/*

  ____  ____ ___      _                    _   
 / ___||  _ \_ _|    / \   __ _  ___ _ __ | |_ 
 \___ \| |_) | |    / _ \ / _` |/ _ \ '_ \| __|
  ___) |  __/| |   / ___ \ (_| |  __/ | | | |_ 
 |____/|_|  |___| /_/   \_\__, |\___|_| |_|\__|
                          |___/                

*/

#include "../goback.hpp"
#include <agent.hpp>
#include <chrono>
#include <cxxopts.hpp>
#include <filesystem>
#include <mads.hpp>
#include <rang.hpp>
#include <array>
#include <format>

// spi
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

using namespace std;
using namespace chrono_literals;
using namespace cxxopts;
using json = nlohmann::json;
using namespace Mads;

#pragma pack(push, 1) // 1 byte alignement, instead of default 4
  struct __attribute__((packed)) Pack{

    uint8_t start;
    float x;
    float y;
    float z;
    float pitch;
    float yaw;
    float feedrate;
    uint8_t check;
  }; // tot byte dimension: 1 + 6*4 + 1 = 26
#pragma pack(pop)

#pragma pack(push, 1)
struct __attribute__((packed)) PackFb {
    uint8_t  start;
    uint32_t msg_id;
    float    x;
    float    y;
    float    z;
    float    error;
    uint8_t  check;
};
#pragma pack(pop)
// Totale: 18 byte fissi

int main(int argc, char *const *argv) {
  // Mads-related
  string settings_uri = SETTINGS_URI;
  bool crypto = false;
  filesystem::path key_dir(Mads::exec_dir() + "/../etc");
  string client_key_name = "client";
  string server_key_name = "broker";
  Mads::auth_verbose auth_verbose = auth_verbose::off;
  string agent_name = argv[0], agent_id;
  chrono::milliseconds period{100};

  // SPI-related
  int _spi = -1;

  // Parse command line options ================================================
  Options options(argv[0]);
  // if needed, add here further CLI options
  // clang-format off
  options.add_options()
    ("p,period", "Sampling period (default 100 ms)", value<size_t>())
    ("n,name", "Agent name (default to fmu_<model name>)", value<string>())
    ("i,agent-id", "Agent ID to be added to JSON frames", value<string>());
  // clang-format on
  SETUP_OPTIONS(options, Agent);
  
  // Load FMU
  agent_name = string("spi_agent");

  // Create Agent
  Agent agent(agent_name, settings_uri);
  if (options_parsed.count("agent-id")) {
    agent.set_agent_id(options_parsed["agent-id"].as<string>());
  }
  if (crypto) {
    agent.set_key_dir(key_dir);
    agent.client_key_name = client_key_name;
    agent.server_key_name = server_key_name;
    agent.auth_verbose = auth_verbose;
  }
  try {
    agent.init(crypto);
  } catch (const std::exception &e) {
    std::cout << fg::red << "Error initializing agent: " << e.what()
              << fg::reset << endl;
    exit(EXIT_FAILURE);
  }

  // Settings
  json settings = agent.get_settings();
  period = chrono::milliseconds(settings.value("period", 100));
  if (options_parsed.count("period") != 0) {
    period = chrono::milliseconds(options_parsed["period"].as<size_t>());
  }

  // Connection
  agent.enable_remote_control();
  agent.connect();
  agent.register_event(event_type::startup);
  agent.info();

  /*
  if (settings.contains("parameters")) {
    cout << style::bold << "Overriding default FMU parameters (" 
         << settings["parameters"].size() << ") from settings:" 
         << style::reset << endl;
    int i = 0;
    for (auto const &[param, value] : settings["parameters"].items()) {
      cout << "  " << param << ": " << style::bold << value 
           << style::reset;
      if (!value.is_number()) {
        cout << fg::red << "  NOT A NUMBER, SKIPPING" << fg::reset;
      } else {
        plant.set_real(param, value.get<double>());
      }
      cout << endl;
      i++;
    }
    cout << "  " << i << " parameters overridden\n\n\n" << endl;
  }
  */

  // SPI bus openining
  _spi = open("/dev/spidev0.0", O_RDWR);
  if(_spi < 0){

    cerr << "ERROR: SPI bus not accessible" << endl;
  }

  uint8_t mode = SPI_MODE_0;
  ioctl(_spi, SPI_IOC_WR_MODE, &mode);
  uint32_t speed = 1000000;
  ioctl(_spi, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

  // loop
  auto last_timestep = chrono::steady_clock::now();
  chrono::steady_clock::time_point now;
  double dt = 0, t = 0, t_in = 0, t_msg = 0;
  json status;
  array<string, 3> console_out;

  Pack pkt = {};
  PackFb fb = {};
  pkt.start = 0xAA;
  fb.start = 0xBB;
  uint32_t last_id = 0;

  agent.loop(
      [&]() -> chrono::milliseconds {
        // timing
        now = chrono::steady_clock::now();
        dt = chrono::duration_cast<chrono::microseconds>(now - last_timestep)
                 .count() /
             1e6;
        last_timestep = now;
        t += dt;

        // read from SPI
        vector<uint8_t> dummy_tx(sizeof(PackFb), 0);

        struct spi_ioc_transfer rx = {};
        rx.tx_buf = (unsigned long)dummy_tx.data();
        rx.rx_buf = (unsigned long)&fb;
        rx.len = sizeof(PackFb);
        rx.speed_hz = 1000000;
        rx.bits_per_word = 8;

        if (ioctl(_spi, SPI_IOC_MESSAGE(1), &rx) < 1) {
          cerr << "ERROR: failed packet reception" << endl;
        }

        uint8_t checksum = 0;
        uint8_t* ptr = (uint8_t*)&fb;
        for(size_t i = 0; i < sizeof(PackFb) - 1; i++) {
          checksum ^= ptr[i];
        }

        if (fb.start != 0xBB || fb.check != checksum) {
          cerr << "ERROR: wrong checksum" << endl;
        } else{

          if(fb.msg_id > last_id){
            
            // new data
            last_id = fb.msg_id;
            status["x"] = fb.x;
            status["y"] = fb.y;
            status["z"] = fb.z;
            status["error"] = fb.error;

            agent.publish(status);
          }
        }

        // input
        if (agent.receive(true) == message_type::json &&
            agent.last_topic() != "setpoint") {
          auto msg = agent.last_message();
          auto in = json::parse(get<1>(msg));

          if (!in["machine"].is_object()) {
            t_msg = t;
            console_out[0] = to_string(t_msg) + " s, Missing /machine/";
            goto integrate;
          }

          t_in = t;
          console_out[1] = to_string(t_in) + " s, " + in["machine"].dump();
          for (auto const &[k, v] : in["machine"].items()) {
            if (v.is_array()) {

              pkt.x = v.value("x", 0.0f);
              pkt.y = v.value("y", 0.0f);
              pkt.z = v.value("z", 0.0f);
              pkt.pitch = v.value("pitch", 0.0f);
              pkt.yaw = v.value("yaw", 0.0f);
              pkt.feedrate = v.value("feedrate", 0.0f);

              // checksum
              uint8_t* ptr = (uint8_t*)&pkt;
              uint8_t checksum = 0;
              for(size_t i = 0; i < sizeof(Pack) - 1; i++) {
                checksum ^= ptr[i];
              }
              pkt.check = checksum;

              // transmit
              struct spi_ioc_transfer tx = {};
              tx.tx_buf = (unsigned long)&pkt;
              tx.len = sizeof(pkt);
              tx.speed_hz = 1000000;
              tx.bits_per_word = 8;

              if (ioctl(_spi, SPI_IOC_MESSAGE(1), &tx) < 1) {
                cerr << "ERROR: failed packet transmission" << endl;
              }

            }  else {
              console_out[1] = " (skipped, not a number or array)";
            }
          }
        }

      integrate:
        // Integrate
        console_out[2] = to_string(t) + " s";

        cout << goback(3) << fg::yellow
             << "Last message: " << console_out[0] << fg::reset << endl
             << "Received: " << console_out[1] << endl
             << "Status update after: " << console_out[2] << endl;
        return 0ms;
      },
      period);

  cout << endl << fg::green << "SPI agent stopped" << fg::reset << endl;
  agent.register_event(event_type::shutdown);
  agent.disconnect();

  if(_spi >= 0){

    close(_spi);
    _spi = -1;
  }
  
  if (agent.restart()) {
    auto cmd = string(MADS_PREFIX) + argv[0];
    cout << "Restarting " << cmd << "..." << endl;
    execvp(cmd.c_str(), argv);
  }
  return 0;
}
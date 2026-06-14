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
#include <stddef.h>
#include "../spi_payload.hpp"

// RT
#include <sched.h>

using namespace std;
using namespace chrono_literals;
using namespace cxxopts;
using json = nlohmann::json;
using namespace Mads;

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

  // RT
  struct sched_param sp;
  memset(&sp, 0, sizeof(sp));
  sp.sched_priority = 98;
  if (sched_setscheduler(0, SCHED_FIFO, &sp) == -1) {
      cerr << fg::yellow << "Warning: Failed to set SCHED_FIFO. Run as root for Real-Time performance." << fg::reset << endl;
  }

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

  uint32_t speed = settings.value("speed", 5000000); // by default, it is set to 5MHz

  period = chrono::milliseconds(settings.value("period", 100));
  if (options_parsed.count("period") != 0) {
    period = chrono::milliseconds(options_parsed["period"].as<size_t>());
  }

  vector<string> tx_vars = settings.value("mosi", vector<string>{});
  vector<string> rx_vars = settings.value("miso", vector<string>{});
  if(tx_vars.empty() || rx_vars.empty()){

    throw runtime_error("Fill mads.ini agent's section with desired MOSI and MISO variables");
  }
  SPIPayload payload;
  payload.init(tx_vars, rx_vars);

  // Connection
  agent.enable_remote_control();
  agent.connect();
  agent.register_event(event_type::startup);
  agent.info();

  // SPI bus openining
  _spi = open("/dev/spidev0.0", O_RDWR);
  if(_spi < 0){

    cerr << "ERROR: SPI bus not accessible" << endl;
  }

  uint8_t mode = SPI_MODE_0;
  ioctl(_spi, SPI_IOC_WR_MODE, &mode);
  ioctl(_spi, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

  // loop
  auto last_timestep = chrono::steady_clock::now();
  chrono::steady_clock::time_point now;
  double dt = 0, t = 0, t_in = 0, t_msg = 0;
  json status;
  array<string, 3> console_out;

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

        // MADS -> SPI
        if (agent.receive(false) == message_type::json && agent.last_topic() == "setpoint") {
          try {
            auto in = json::parse(get<1>(agent.last_message()));
            if (in.contains("spi_input") && in["spi_input"].is_object()) {
              auto spi_json = in["spi_input"];
              
              bool flag = spi_json.value("gflag", false);
              uint8_t start_byte = flag ? 0xCC : 0xAA;

              payload.pack_tx(start_byte, last_id, spi_json);
            }
          } catch (const json::parse_error& e) {
            std::cerr << "Json parsing error: " << e.what() << std::endl;
            return 0ms;
          }
        }

        /*
          _____      _ _   ____              _             ____  ____ ___ 
         |  ___|   _| | | |  _ \ _   _ _ __ | | _____  __ / ___||  _ \_ _|
         | |_ | | | | | | | | | | | | | '_ \| |/ _ \ \/ / \___ \| |_) | | 
         |  _|| |_| | | | | |_| | |_| | |_) | |  __/>  <   ___) |  __/| | 
         |_|   \__,_|_|_| |____/ \__,_| .__/|_|\___/_/\_\ |____/|_|  |___|
                                      |_|                                 
        */

        struct spi_ioc_transfer tr;
        memset(&tr, 0, sizeof(tr));
        tr.tx_buf = (unsigned long)payload.get_tx_data();
        tr.rx_buf = (unsigned long)payload.get_rx_data();
        tr.len = payload.get_total_bytes();
        tr.speed_hz = 5000000;
        tr.bits_per_word = 8;
        
        if(ioctl(_spi, SPI_IOC_MESSAGE(1), &tr) < 0){
            perror("SPI Communication error");
        }

        json status = payload.unpack_rx();
        
        if (!status.empty()) {
          uint32_t rx_msg_id = status.value("msg_id", 0);
          
          if (rx_msg_id > last_id) {
            last_id = rx_msg_id;
            agent.publish(status);
          }
        }

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
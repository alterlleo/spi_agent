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
#include <vector>
#include <string>
#include <map>
#include <algorithm>

// RT
#include <sched.h>

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
    float a;
    float c;
    float vx;
    float vy;
    uint8_t check;
    uint8_t padding[66];
  }; // tot byte dimension: 1 + 7*4 + 1 + 2 = 32
#pragma pack(pop)

#pragma pack(push, 1)
struct __attribute__((packed)) PackFb {
    uint8_t  start;
    uint32_t msg_id;
    float    x;
    float    y;
    float    z;
    float    a;
    float    c;
    float    vx;
    float    vy;
    float    vz;
    float    va;
    float    vc;
    float    ax;
    float    ay;
    float    az;
    float    aa;
    float    ac;
    float    error;
    uint8_t  check;
    uint8_t padding[26];
};
#pragma pack(pop)

struct __attribute__((packed)) SPIFrame{
  Pack tx;
  PackFb rx;
};

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
  period = chrono::milliseconds(settings.value("period", 100));
  if (options_parsed.count("period") != 0) {
    period = chrono::milliseconds(options_parsed["period"].as<size_t>());
  }

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
  uint32_t speed = 5000000;
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

        // MADS -> SPI
        if (agent.receive(false) == message_type::json && agent.last_topic() == "setpoint") {
          
          auto msg = agent.last_message();
          auto in = json::parse(get<1>(msg));

          if (in.contains("fmu_input") && in["fmu_input"].is_object()) {
            auto fmu = in["fmu_input"];
            
            pkt.x = fmu.value("x", 0.0f);
            pkt.y = fmu.value("y", 0.0f);
            pkt.z = fmu.value("z", 0.0f);
            pkt.a = fmu.value("a", 0.0f);
            pkt.c = fmu.value("c", 0.0f);
            pkt.vx = fmu.value("vx", 0.0f);
            pkt.vy = fmu.value("vy", 0.0f);

            if(pkt.x == 0.0f && pkt.y == 0.0f && pkt.z == 0.0f && pkt.a == 0.0f && pkt.c == 0.0f){
              pkt.start = 0xCC;
            } else {
              pkt.start = 0xAA;
            }

            uint8_t checksum_tx = 0;
            uint8_t* ptr_tx = (uint8_t*)&pkt;
            for(size_t i = 0; i < offsetof(Pack, check); i++) {
                checksum_tx ^= ptr_tx[i];
            }
            pkt.check = checksum_tx;

          } else {
            console_out[0] = to_string(t) + " s, Missing /fmu_input/";
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

        SPIFrame frame = {};
        frame.tx = pkt;

        struct spi_ioc_transfer tr;
        memset(&tr, 0, sizeof(tr));
        tr.tx_buf = (unsigned long)&pkt;
        tr.rx_buf = (unsigned long)&fb;
        tr.len = sizeof(Pack);
        tr.speed_hz = 5000000;
        tr.bits_per_word = 8;
        
        if(ioctl(_spi, SPI_IOC_MESSAGE(1), &tr) < 0){
            fprintf(stderr, "SPI Communication error. Attempted length: %d\n", tr.len);
            perror("System Error");
        }

        // SPI -> MADS
        uint8_t checksum_rx = 0;
        uint8_t* ptr_rx = (uint8_t*)&fb;
        for(size_t i = 0; i < offsetof(PackFb, check); i++) {
          checksum_rx ^= ptr_rx[i];
        }

        if (fb.start != 0xBB || fb.check != checksum_rx) {
            cerr << "ERROR! Calc_Check: 0x" << hex << (int)checksum_rx 
                 << " | Recv_Check: 0x" << (int)fb.check 
                 << " | Start: 0x" << (int)fb.start << dec << endl;
        } else {

          if(fb.msg_id > last_id){
            
            // new data
            last_id = fb.msg_id;
            status["position"] = {(float)(fb.x), (float)(fb.y), (float)(fb.z), (float)(fb.a), (float)(fb.c)};
            status["velocity"] = {(float)(fb.vx), (float)(fb.vy), (float)(fb.vz), (float)(fb.va), (float)(fb.vc)};
            status["acceleration"] = {(float)(fb.ax), (float)(fb.ay), (float)(fb.az), (float)(fb.aa), (float)(fb.ac)};
            status["error"] = (float)(fb.error);

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
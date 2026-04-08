#define TOML_IMPLEMENTATION 0
#include <agent.hpp>
#include <iostream>


int main(int argc, char *argv[]) {
  auto agent = Mads::start_agent(argv[1], SETTINGS_URI);
  cout << "Settings:\n" << agent->get_settings().dump(2) << endl;
  return 0;
}
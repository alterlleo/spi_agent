#   _____                     _        _     _           
#  | ____|_  _____  ___ _   _| |_ __ _| |__ | | ___  ___ 
#  |  _| \ \/ / _ \/ __| | | | __/ _` | '_ \| |/ _ \/ __|
#  | |___ >  <  __/ (__| |_| | || (_| | |_) | |  __/\__ \
#  |_____/_/\_\___|\___|\__,_|\__\__,_|_.__/|_|\___||___/
#  
if (APPLE)
  set(CMAKE_INSTALL_RPATH "@executable_path/../lib")
  # include_directories(/opt/homebrew/include)
  # link_directories(/opt/homebrew/lib)
else()
  set(CMAKE_INSTALL_RPATH "\$ORIGIN/../lib;/usr/local/lib")
endif()
if(WIN32)
  add_compile_options(/wd4005)
endif()

# Agent
add_executable(mads-spi spi_agent.cpp)
target_include_directories(mads-spi BEFORE PRIVATE ${CMAKE_SOURCE_DIR}/src/compat)
target_include_directories(mads-spi PRIVATE ${cxxopts_SOURCE_DIR}/include)
if(WIN32)
  target_compile_definitions(mads-spi PRIVATE _HAS_STD_BYTE=0)
  link_directories(${MADS_PREFIX}/lib ${MADS_PREFIX}/bin)
  target_link_libraries(mads-spi PRIVATE Ws2_32 bcrypt Secur32 Crypt32 Dnsapi IPHLPAPI MadsCore)
else()
  target_link_libraries(mads-spi PRIVATE MadsCore)
endif()

add_executable(simple simple.cpp)
target_link_libraries(simple PRIVATE MadsCore)

#   ___           _        _ _ 
#  |_ _|_ __  ___| |_ __ _| | |
#   | || '_ \/ __| __/ _` | | |
#   | || | | \__ \ || (_| | | |
#  |___|_| |_|___/\__\__,_|_|_|

if(MADS_INSTALL_AGENT)
  install(TARGETS mads-spi
    RUNTIME DESTINATION bin
  )
endif()
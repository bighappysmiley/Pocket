cmake_minimum_required(VERSION 3.16)
project(pocket_host LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(POCKET_UI ${CMAKE_SOURCE_DIR}/../components/pocket_ui)

add_executable(pocket_host
  sim_main.cpp
  ${POCKET_UI}/src/input.cpp
  ${POCKET_UI}/src/canvas.cpp
  ${POCKET_UI}/src/refresh.cpp
  ${POCKET_UI}/src/nav.cpp
  ${POCKET_UI}/src/chrome.cpp
  ${POCKET_UI}/src/config_store.cpp
  ${POCKET_UI}/src/pin.cpp
  ${POCKET_UI}/src/app.cpp
  ${POCKET_UI}/src/screens_lock.cpp
  ${POCKET_UI}/src/screens_onboarding.cpp
  ${POCKET_UI}/src/screens_home.cpp
  ${POCKET_UI}/src/screens_apps.cpp
  ${POCKET_UI}/src/screens_settings.cpp
  ${POCKET_UI}/src/cloud_client.cpp
)

target_include_directories(pocket_host PRIVATE ${POCKET_UI}/include)
target_compile_definitions(pocket_host PRIVATE POCKET_HOST=1)

set(POCKET_UI_SRCS
  ${POCKET_UI}/src/input.cpp
  ${POCKET_UI}/src/canvas.cpp
  ${POCKET_UI}/src/refresh.cpp
  ${POCKET_UI}/src/nav.cpp
  ${POCKET_UI}/src/chrome.cpp
  ${POCKET_UI}/src/config_store.cpp
  ${POCKET_UI}/src/pin.cpp
  ${POCKET_UI}/src/app.cpp
  ${POCKET_UI}/src/screens_lock.cpp
  ${POCKET_UI}/src/screens_onboarding.cpp
  ${POCKET_UI}/src/screens_home.cpp
  ${POCKET_UI}/src/screens_apps.cpp
  ${POCKET_UI}/src/screens_settings.cpp
  ${POCKET_UI}/src/cloud_client.cpp
)

enable_testing()
add_executable(test_input ../tests/test_input.cpp ${POCKET_UI}/src/input.cpp)
target_include_directories(test_input PRIVATE ${POCKET_UI}/include)
add_test(NAME test_input COMMAND test_input)

add_executable(test_onboarding ../tests/test_onboarding_flow.cpp ${POCKET_UI_SRCS})
target_include_directories(test_onboarding PRIVATE ${POCKET_UI}/include)
target_compile_definitions(test_onboarding PRIVATE POCKET_HOST=1)
add_test(NAME test_onboarding COMMAND test_onboarding)
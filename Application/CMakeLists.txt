cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(fnirs_fw)

target_sources(app PRIVATE
	main.c
	src/m_i2c.c
	src/m_i2c_rtc.c
	src/m_i2c_ring_buffer.c
	src/m_i2c_led.c
	src/m_i2c_as7341.c
	src/m_ble.c
	src/m_ble_batch.c
	src/m_ble_gatt.c
	src/m_ble_proto.c
	src/m_ctrl.c
)

target_include_directories(app PRIVATE include)

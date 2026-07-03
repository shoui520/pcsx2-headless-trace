add_executable(pcsx2-trace)

target_sources(pcsx2-trace PRIVATE
	HeadlessHost.cpp
	Main.cpp
)

target_compile_definitions(pcsx2-trace PRIVATE
	PCSX2_TRACE_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(pcsx2-trace PRIVATE
	PCSX2_FLAGS
	PCSX2
	common
)

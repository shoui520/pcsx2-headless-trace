add_executable(pcsx2-trace)

target_sources(pcsx2-trace PRIVATE
	HeadlessHost.cpp
	Main.cpp
)

target_link_libraries(pcsx2-trace PRIVATE
	PCSX2_FLAGS
	PCSX2
	common
)

add_custom_command(TARGET pcsx2-trace POST_BUILD
	COMMAND "${CMAKE_COMMAND}" -E copy_directory
		"${CMAKE_SOURCE_DIR}/bin/resources"
		"$<TARGET_FILE_DIR:pcsx2-trace>/resources"
)

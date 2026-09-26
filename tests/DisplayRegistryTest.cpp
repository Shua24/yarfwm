#include "RegistryInterfaces.hpp"
#include <gtest/gtest.h>

TEST(RegistryInterfaces, RecognisesTheSixGlobalsTheWindowManagerBinds)
{
	EXPECT_EQ(classify_registry_interface("river_window_manager_v1"),
		  registry_interface_window_manager);
	EXPECT_EQ(classify_registry_interface("river_layer_shell_v1"),
		  registry_interface_layer_shell);
	EXPECT_EQ(classify_registry_interface("river_xkb_bindings_v1"),
		  registry_interface_xkb_bindings);
	EXPECT_EQ(classify_registry_interface("wl_compositor"),
		  registry_interface_compositor);
	EXPECT_EQ(classify_registry_interface("wl_shm"),
		  registry_interface_shared_memory);
	// wl_seat was deliberately ignored until the decoration feature: a
	// titlebar press arrives as a wl_pointer event on our own surface, and
	// the pointer comes from the seat.
	EXPECT_EQ(classify_registry_interface("wl_seat"),
		  registry_interface_seat);
}

TEST(RegistryInterfaces, IgnoresEverythingElse)
{
	// wl_output is bound through river_layer_shell_v1's get_output, not
	// from the registry, so the registry classifier still ignores it.
	EXPECT_EQ(classify_registry_interface("wl_output"),
		  registry_interface_unknown);
	EXPECT_EQ(
	    classify_registry_interface("zwlr_virtual_pointer_manager_v1"),
	    registry_interface_unknown);
	EXPECT_EQ(classify_registry_interface(nullptr),
		  registry_interface_unknown);
}

#ifndef REGISTRY_INTERFACES_HPP
#define REGISTRY_INTERFACES_HPP

// Which registry global an interface name names. The window manager binds only
// these; everything else the compositor advertises is ignored on purpose.
enum RegistryInterfaceKind {
	registry_interface_unknown = 0,
	registry_interface_window_manager,
	registry_interface_layer_shell,
	registry_interface_xkb_bindings,
	registry_interface_compositor,
	registry_interface_shared_memory,
	// wl_seat is bound for one reason only: a press on a decoration
	// surface arrives as a wl_pointer event on the window manager's own
	// surface, so the Seat needs a pointer, and the pointer comes from the
	// seat. Nothing else about the seat is used.
	registry_interface_seat,
};

// Pure: no Wayland call, so the classification is unit testable.
RegistryInterfaceKind classify_registry_interface(const char *interface_name);

#endif // REGISTRY_INTERFACES_HPP

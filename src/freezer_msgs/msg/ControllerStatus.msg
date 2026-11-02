# Whether the node talks to the Freezer controller, published on ~/status,
# e.g. /freezer/status. Latched: published when it changes and every second,
# so that a subscriber that stops receiving it knows that the node is gone.

builtin_interfaces/Time stamp

# True from a successful handshake until the controller stops answering. The
# node then tries again every reconnect_period, and connects once it is back.
bool connected

# The serial port of the controller, e.g. /dev/ttyUSB0, or "fake" for the
# fake controller.
string device

# Why it is not connected, e.g. the error of the serial port; empty when it is.
string message

# Status queries the controller left unanswered during the shots since the
# node started, whether the shots succeeded or not. A healthy controller leaves
# none: more than 0 is a fault to look into, e.g. its firmware or its USB cable.
uint32 missed_status

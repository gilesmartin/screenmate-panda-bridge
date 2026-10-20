#include "../native/session_inventory.h"
#include <cassert>
#include <string>

int main() {
    const std::string stock = "> CAN stream sessions: 1\n  id=1 filters=75 window=10ms capacity=64 write_index=42\n";
    const std::string none = "> CAN stream sessions: 0\n";
    const std::string own = "> CAN stream sessions: 1\n  id=1 filters=1 window=128ms capacity=512 write_index=22\n";
    const std::string own_and_late_stock = "> CAN stream sessions: 2\n  id=1 filters=1 window=128ms capacity=512\n  id=2 filters=75 window=10ms capacity=64\n";
    const std::string unknown = "> CAN stream sessions: 2\n  id=1 filters=1\n  id=3 filters=3\n";
    assert(inventory_matches(stock, 1, -1, false));
    assert(!inventory_matches(none, 1, -1, false));
    assert(inventory_matches(none, -1, -1, true));
    assert(!inventory_matches(stock, -1, -1, true));
    assert(inventory_matches(own, -1, 1, true));
    assert(inventory_matches(own_and_late_stock, -1, 1, true));
    assert(!inventory_matches(unknown, -1, 1, true));
    assert(!inventory_matches(own, 1, 1, false));
    assert(!inventory_matches(own, -1, -1, true));
    return 0;
}

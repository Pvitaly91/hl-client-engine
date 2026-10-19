#pragma once
#include <functional>
#include <string_view>
namespace hlclient::console {
struct SessionActions {
    std::function<bool(std::string_view,std::string_view)> request;
    std::function<bool(unsigned)> select_slot;
    std::function<bool(int)> cycle_inventory;
    std::function<bool()> last_inventory;
    std::function<bool(std::string_view)> select_weapon;
};
}

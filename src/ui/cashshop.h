// Item shop in the in-game menu (the original's cash store, paid with gold): data and purchase (cashshop.cpp)
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace cash {
struct Product {
    int index = 0; // grant table entry
    int type = 0, id = 0, count = 0, price = 0, owned = 0;
    std::string name, icon, type_name, desc;
};
void capture();                         // game thread, while the menu is open
std::vector<Product> pull();            // main thread
void buy(int index);                    // posts the purchase of grant table entry `index` to the game thread
bool result(uint32_t *seen, int *code); // new purchase result: 0 bought, 1 not enough gold, 2 no room, 3 failed
int harz_to_gold(int harz);             // the port's gold price of an original Harz (paid currency) price
}

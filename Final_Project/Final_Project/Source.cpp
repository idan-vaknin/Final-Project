#include "Party.h"
#include <iostream>

using std::cout;
using std::endl;

int main() {
	try {
		Party p = Party(2, 122323);
		p.connectToAllParties();
	}
	catch (std::exception & exc) {
		cout << exc.what() << endl;
	}
	return 0;
}
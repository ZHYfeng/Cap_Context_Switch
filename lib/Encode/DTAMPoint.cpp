#include "DTAMPoint.h"

DTAMPoint::DTAMPoint(std::string _name, std::vector<unsigned> _vectorClock) :
		name(_name), isTaint(false) {
	for (unsigned i = 0; i < _vectorClock.size(); i++) {
		vectorClock.push_back(_vectorClock[i]);
	}
}

DTAMPoint::~DTAMPoint() {

}

bool DTAMPoint::operator<=(DTAMPoint *point) {
	unsigned before = 0, after = 0, equal = 0;
	for (unsigned i = 0; i < vectorClock.size(); i++) {
		if (vectorClock[i] > point->vectorClock[i]) {
			after++;
		} else if (vectorClock[i] < point->vectorClock[i]) {
			before++;
		} else {
			equal++;
		}
	}
	if (before == 0 && after > 0) {
		return false;
	} else {
		return true;
	}
}


#ifndef LIB_CORE_DTAMPOINT_H_
#define LIB_CORE_DTAMPOINT_H_

#include <string>
#include <vector>

class DTAMPoint {
	public:
		std::string name;
		bool isTaint;
		std::vector<DTAMPoint*> affectingPoint;
		std::vector<DTAMPoint*> affectedPoint;
		std::vector<unsigned> vectorClock;

	public:
		DTAMPoint(std::string _name, std::vector<unsigned> _vectorClock);
		virtual ~DTAMPoint();
		bool operator<=(DTAMPoint *point);
};

#endif /* LIB_CORE_DTAMPOINT_H_ */

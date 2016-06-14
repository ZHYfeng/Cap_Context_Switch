/*
 * VectorClock.h
 *
 *  Created on: Feb 24, 2016
 *      Author: zhy
 */

#ifndef LIB_CORE_DTAM_
#define LIB_CORE_DTAM_

#include <sys/time.h>
#include <map>
#include <set>
#include <string>

#include "DTAMPoint.h"
#include "FilterSymbolicExpr.h"
#include "RuntimeDataManager.h"
#include "Trace.h"

namespace klee {

class DTAM {
	private:
		RuntimeDataManager* runtimeData;
		Trace* trace;
		std::map<std::string, DTAMPoint*> allWrite;
		std::map<std::string, DTAMPoint*> allRead;
		struct timeval start, finish;
		double cost;
		FilterSymbolicExpr filter;

	public:
		DTAM(RuntimeDataManager* data);
		virtual ~DTAM();

		void DTAMParallel();
		void DTAMhybrid();
		void initTaint(std::vector<DTAMPoint*> &remainPoint);
		void propagateTaint(std::vector<DTAMPoint*> &remainPoint);
		void getTaint(std::set<std::string> &taint);
		void dtam();

};

}
#endif /* LIB_CORE_DTAM_ */


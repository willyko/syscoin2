#include "test/test_syscoin_services.h"
#include "utiltime.h"
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE (syscoin_tests, SyscoinTestingSetup)

BOOST_AUTO_TEST_CASE (generate_200blocks)
{
  int height, timeoutCounter;
  CallRPC("node1", "generate 200");
  UniValue r;
  r = CallRPC("node1", "getinfo");
  height = find_value(r.get_obj(), "blocks").get_int();
  BOOST_CHECK(height == 200);
  height = 0;
  timeoutCounter = 0;
  while(height != 200)
  {
	  MilliSleep(100);
	  height = find_value(r.get_obj(), "blocks").get_int();
	  r = CallRPC("node2", "getinfo");
	  timeoutCounter++;
	  if(timeoutCounter > 100)
		  break;
  }
  BOOST_CHECK(height == 200);
  height = 0;
  timeoutCounter = 0;
  while(height != 200)
  {
	  MilliSleep(100);
	  height = find_value(r.get_obj(), "blocks").get_int();
	  r = CallRPC("node3", "getinfo");
	  timeoutCounter++;
	  if(timeoutCounter > 100)
		  break;
  }
  BOOST_CHECK(height == 200);
  height = 0;
  timeoutCounter = 0;
}


BOOST_AUTO_TEST_SUITE_END ()
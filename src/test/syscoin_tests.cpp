#include "test/test_syscoin_services.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE (syscoin_tests, SyscoinTestingSetup)

BOOST_AUTO_TEST_CASE (generate_200blocks)
{
  CallRPC("node1", "generate 200");
  UniValue r;
  r = CallRPC("node1", "getinfo");
  BOOST_CHECK(find_value(r.get_obj(), "blocks").get_int() == 200);
  r = CallRPC("node2", "getinfo");
  BOOST_CHECK(find_value(r.get_obj(), "blocks").get_int() == 200);
  r = CallRPC("node3", "getinfo");
  BOOST_CHECK(find_value(r.get_obj(), "blocks").get_int() == 200);
}


BOOST_AUTO_TEST_SUITE_END ()
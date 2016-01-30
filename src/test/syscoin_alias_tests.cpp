#include "test/test_syscoin_services.h"
#include "utiltime.h"
#include "rpcserver.h"
#include <boost/test/unit_test.hpp>
BOOST_GLOBAL_FIXTURE( SyscoinTestingSetup );

BOOST_FIXTURE_TEST_SUITE (syscoin_alias_tests, BasicSyscoinTestingSetup)

BOOST_AUTO_TEST_CASE (generate_sysrates_alias)
{
	CreateSysRatesIfNotExist();
}
BOOST_AUTO_TEST_CASE (generate_big_aliasdata)
{
	GenerateBlocks(50);
	// 1023 bytes long
	string gooddata = "asdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfssdsfsdfsdfsdfsdfsdsdfdfsdfsdfsdfsd";
	// 1024 bytes long
	string baddata =   "asdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfssdsfsdfsdfsdfsdfsdsdfdfsdfsdfsdfsdz";
	AliasNew("node1", "jag", gooddata);
	BOOST_CHECK_THROW(CallRPC("node1", "aliasnew jag1 " + baddata), runtime_error);
}
BOOST_AUTO_TEST_CASE (generate_big_aliasname)
{
	GenerateBlocks(50);
	// 255 bytes long
	string goodname = "SfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsDfdfdd";
	// 1023 bytes long
	string gooddata = "asdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfasdfasdfsadfsadassdsfsdfsdfsdfsdfsdsdfssdsfsdfsdfsdfsdfsdsdfdfsdfsdfsdfsd";	
	// 256 bytes long
	string badname =   "SfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsDfdfddz";
	AliasNew("node1", goodname, "a");
	BOOST_CHECK_THROW(CallRPC("node1", "aliasnew " + badname + " 3d"), runtime_error);
}
BOOST_AUTO_TEST_CASE (generate_aliasupdate)
{
	GenerateBlocks(1);
	// update an alias that isn't yours
	BOOST_CHECK_THROW(CallRPC("node2", "aliasupdate jag test"), runtime_error);
	AliasUpdate("node1", "jag", "changeddata");
	// shouldnt update data, just uses prev data because it hasnt changed
	AliasUpdate("node1", "jag", "changeddata");

}
BOOST_AUTO_TEST_CASE (generate_sendmoneytoalias)
{
	GenerateBlocks(200, "node2");
	AliasNew("node2", "sendnode2", "changeddata2");
	UniValue r;
	// get balance of node2 first to know we sent right amount oater
	BOOST_CHECK_NO_THROW(r = CallRPC("node2", "getinfo"));
	CAmount balanceBefore = AmountFromValue(find_value(r.get_obj(), "balance"));
	BOOST_CHECK_THROW(CallRPC("node1", "sendtoaddress sendnode2 1.335"), runtime_error);
	GenerateBlocks(1);
	BOOST_CHECK_NO_THROW(r = CallRPC("node2", "getinfo"));
	// 8.25 since 1 block matures
	balanceBefore += 1.335*COIN + 8.25*COIN;
	CAmount balanceAfter = AmountFromValue(find_value(r.get_obj(), "balance"));
	BOOST_CHECK_EQUAL(balanceBefore, balanceAfter);
}
BOOST_AUTO_TEST_CASE (generate_aliastransfer)
{
	GenerateBlocks(200, "node2");
	GenerateBlocks(200, "node3");
	// shouldnt update data, just uses prev data because it hasnt changed
	AliasNew("node1", "jagnode1", "changeddata1");
	AliasNew("node2", "jagnode2", "changeddata2");
	AliasNew("node3", "jagnode3", "changeddata3");
	UniValue r;
	BOOST_CHECK_NO_THROW(CallRPC("node1", "aliasupdate jagnode1 changeddata1 jagnode2"));
	GenerateBlocks(1);
	// check its not mine anymore
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "aliasinfo jagnode1"));
	BOOST_CHECK(find_value(r.get_obj(), "ismine").get_bool() == false);
	// it got xferred to right person
	BOOST_CHECK_NO_THROW(r = CallRPC("node2", "aliasinfo jagnode1"));
	BOOST_CHECK(find_value(r.get_obj(), "ismine").get_bool() == true);

	// xfer an alias that isn't yours
	BOOST_CHECK_THROW(CallRPC("node1", "aliasupdate jagnode1 changeddata1 jagnode2"), runtime_error);
	GenerateBlocks(1);

	// trasnfer alias and update it at the same time
	BOOST_CHECK_NO_THROW(CallRPC("node2", "aliasupdate jagnode2 changeddata4 jagnode3"));
	GenerateBlocks(1, "node2");
	// check its not mine anymore
	BOOST_CHECK_NO_THROW(r = CallRPC("node2", "aliasinfo jagnode2"));
	BOOST_CHECK(find_value(r.get_obj(), "ismine").get_bool() == false);
	BOOST_CHECK(find_value(r.get_obj(), "value").get_str() == string("changeddata4"));
	// check xferred right person and data changed
	BOOST_CHECK_NO_THROW(r = CallRPC("node3", "aliasinfo jagnode2"));
	BOOST_CHECK(find_value(r.get_obj(), "value").get_str() == string("changeddata4"));
	BOOST_CHECK(find_value(r.get_obj(), "ismine").get_bool() == true);

	// update xferred alias
	BOOST_CHECK_NO_THROW(CallRPC("node2", "aliasupdate jagnode1 changeddata5"));
	GenerateBlocks(1, "node2");
	BOOST_CHECK_NO_THROW(r = CallRPC("node2", "aliasinfo jagnode1"));
	BOOST_CHECK(find_value(r.get_obj(), "value").get_str() == string("changeddata5"));
	BOOST_CHECK(find_value(r.get_obj(), "ismine").get_bool() == true);

	// retransfer alias
	BOOST_CHECK_NO_THROW(CallRPC("node2", "aliasupdate jagnode1 changeddata5 jagnode3"));
	GenerateBlocks(1, "node2");
	BOOST_CHECK_NO_THROW(r = CallRPC("node3", "aliasinfo jagnode1"));
	BOOST_CHECK(find_value(r.get_obj(), "value").get_str() == string("changeddata5"));
	BOOST_CHECK(find_value(r.get_obj(), "ismine").get_bool() == true);
}
BOOST_AUTO_TEST_SUITE_END ()
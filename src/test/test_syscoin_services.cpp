#include "test_syscoin_services.h"
#include "utiltime.h"


#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/algorithm/string/predicate.hpp>
// SYSCOIN testing setup
SyscoinTestingSetup::SyscoinTestingSetup()
{
	StartNode("node1");
	StartNode("node2");
	StartNode("node3");
}
void SyscoinTestingSetup::StartNode(const string &dataDir)
{
	string nodePath = string("../syscoind -datadir=") + dataDir + string(" -regtest");
	string response = CallExternal(nodePath);
	UniValue val = CallRPC(dataDir, "getinfo");
	while (val.isNull())
	{
		MilliSleep(5000);
		val = CallRPC(dataDir, "getinfo");
	}
}
UniValue SyscoinTestingSetup::CallRPC(const string &dataDir, const string& commandWithArgs)
{
	UniValue val;
	string path = string("../syscoin-cli -datadir=") + dataDir + string(" -regtest ") + commandWithArgs;
	string rawJson = CallExternal(path);
    val.read(rawJson);
	return val;
}
std::string SyscoinTestingSetup::CallExternal(std::string &cmd)
{
	FILE *fp = popen(cmd.c_str(), "r");
	string response;
	if (!fp)
	{
		return response;
	}
	while (!feof(fp))
	{
		char buffer[500];
		// read in the line and make sure it was successful
		if (fgets(buffer,500,fp) != NULL)
		{
			response += string(buffer);
		}

	}
	pclose(fp);
	return response;
}
SyscoinTestingSetup::~SyscoinTestingSetup()
{
	CallRPC("node1", "stop");
	CallRPC("node2", "stop");
	CallRPC("node3", "stop");
    boost::filesystem::remove_all(boost::filesystem::path("node1/regtest"));
	boost::filesystem::remove_all(boost::filesystem::path("node2/regtest"));
	boost::filesystem::remove_all(boost::filesystem::path("node3/regtest"));
}
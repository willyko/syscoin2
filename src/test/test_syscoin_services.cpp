#include "test_syscoin_services.h"
#include "utiltime.h"
#include "util.h"



#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/thread.hpp>

// SYSCOIN testing setup
SyscoinTestingSetup::SyscoinTestingSetup()
{
	StartNode("node1");
	StartNode("node2");
	StartNode("node3");
}
void SyscoinTestingSetup::StartNode(const string &dataDir)
{
    boost::filesystem::path fpath = boost::filesystem::system_complete("../syscoind");
	string nodePath = fpath.string() + string(" -datadir=") + dataDir + string(" -regtest");
    boost::thread t(runCommand, nodePath);
	UniValue val;
	printf("Launching %s, waiting 5 seconds before trying to ping...\n", dataDir.c_str());
	MilliSleep(5000);
	while (val.isNull())
	{
		val = CallRPC(dataDir, "getinfo");
		if(!val.isNull())
			break;
		printf("Waiting for %s to come online, trying again in 5 seconds...\n", dataDir.c_str());
		MilliSleep(5000);
	}
}
UniValue SyscoinTestingSetup::CallRPC(const string &dataDir, const string& commandWithArgs)
{
	UniValue val;
	boost::filesystem::path fpath = boost::filesystem::system_complete("../syscoin-cli");
	string path = fpath.string() + string(" -datadir=") + dataDir + string(" -regtest ") + commandWithArgs;
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
		char buffer[512];
		if (fgets(buffer,sizeof(buffer),fp) != NULL)
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
	MilliSleep(10000);
	if(boost::filesystem::exists(boost::filesystem::system_complete("node1/regtest"))
		boost::filesystem::remove_all(boost::filesystem::system_complete("node1/regtest"));
	if(boost::filesystem::exists(boost::filesystem::system_complete("node2/regtest"))
		boost::filesystem::remove_all(boost::filesystem::system_complete("node2/regtest"));
	if(boost::filesystem::exists(boost::filesystem::system_complete("node3/regtest"))
		boost::filesystem::remove_all(boost::filesystem::system_complete("node3/regtest"));
}
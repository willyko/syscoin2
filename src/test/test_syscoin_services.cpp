#include "test_syscoin_services.h"
#include "utiltime.h"
#include "util.h"



#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/thread.hpp>
#include <boost/lexical_cast.hpp>

// SYSCOIN testing setup
void End()
{
	try
	{
		StopNodes();
	}
	catch(...)
	{
	}
	if(boost::filesystem::exists(boost::filesystem::system_complete("node1/regtest")))
		boost::filesystem::remove_all(boost::filesystem::system_complete("node1/regtest"));
	MilliSleep(1000);
	if(boost::filesystem::exists(boost::filesystem::system_complete("node2/regtest")))
		boost::filesystem::remove_all(boost::filesystem::system_complete("node2/regtest"));
	MilliSleep(1000);
	if(boost::filesystem::exists(boost::filesystem::system_complete("node3/regtest")))
		boost::filesystem::remove_all(boost::filesystem::system_complete("node3/regtest"));
}
void Begin()
{
	try
	{
		End();
	}
	catch(...)
	{
	}
	StartNodes();
}
void StartNodes()
{
	printf("Starting 3 nodes in a regtest setup...\n");
	StartNode("node1");
	StartNode("node2");
	StartNode("node3");
}
void StopNodes()
{
	printf("Stopping node1..\n");
	CallRPC("node1", "stop");
	MilliSleep(3000);
	printf("Stopping node2..\n");
	CallRPC("node2", "stop");
	MilliSleep(3000);
	printf("Stopping node3..\n");
	CallRPC("node3", "stop");
	MilliSleep(3000);
	printf("Done!\n");
}
void StartNode(const string &dataDir)
{
    boost::filesystem::path fpath = boost::filesystem::system_complete("../syscoind");
	string nodePath = fpath.string() + string(" -datadir=") + dataDir + string(" -regtest");
    boost::thread t(runCommand, nodePath);
	UniValue val;
	printf("Launching %s, waiting 3 seconds before trying to ping...\n", dataDir.c_str());
	MilliSleep(3000);
	while (val.isNull())
	{
		val = CallRPC(dataDir, "getinfo");
		if(!val.isNull())
			break;
		printf("Waiting for %s to come online, trying again in 5 seconds...\n", dataDir.c_str());
		MilliSleep(5000);
	}
	printf("Done!\n");
}
UniValue CallRPC(const string &dataDir, const string& commandWithArgs)
{
	UniValue val;
	boost::filesystem::path fpath = boost::filesystem::system_complete("../syscoin-cli");
	string path = fpath.string() + string(" -datadir=") + dataDir + string(" -regtest ") + commandWithArgs;
	string rawJson = CallExternal(path);
    val.read(rawJson);
	return val;
}
std::string CallExternal(std::string &cmd)
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
// generate n Blocks, with up to 10 seconds relay time buffer for other nodes to get the blocks.
// may fail if your network is slow or you try to generate too many blocks such that can't relay within 10 seconds
void GenerateBlocks(int nBlocks)
{
  int height, oldheight, timeoutCounter;
  UniValue r;
  r = CallRPC("node1", "getinfo");
  BOOST_CHECK(!r.isNull());
  oldheight = find_value(r.get_obj(), "blocks").get_int();

  r = CallRPC("node1", "generate " + boost::lexical_cast<std::string>(nBlocks));
  BOOST_CHECK(!r.isNull());
  r = CallRPC("node1", "getinfo");
  BOOST_CHECK(!r.isNull());
  height = find_value(r.get_obj(), "blocks").get_int();
  BOOST_CHECK(height == oldheight+nBlocks);
  height = 0;
  timeoutCounter = 0;
  while(height != nBlocks)
  {
	  MilliSleep(100);
	  r = CallRPC("node2", "getinfo");
	  BOOST_CHECK(!r.isNull());
	  height = find_value(r.get_obj(), "blocks").get_int();
	  timeoutCounter++;
	  if(timeoutCounter > 100)
		  break;
  }
  BOOST_CHECK(height == oldheight+nBlocks);
  height = 0;
  timeoutCounter = 0;
  while(height != nBlocks)
  {
	  MilliSleep(100);
	  r = CallRPC("node3", "getinfo");
	  BOOST_CHECK(!r.isNull());
	  height = find_value(r.get_obj(), "blocks").get_int();
	  timeoutCounter++;
	  if(timeoutCounter > 100)
		  break;
  }
  BOOST_CHECK(height == oldheight+nBlocks);
  height = 0;
  timeoutCounter = 0;
}
void AliasNew(const string& aliasname, const string& aliasdata)
{
	UniValue r;
	r = CallRPC("node1", "aliasnew " + aliasname + " " + aliasdata);
	GenerateBlocks(1);
	r = CallRPC("node1", "aliasinfo " + aliasname);
	BOOST_CHECK(!r.isNull());
	BOOST_CHECK(find_value(r.get_obj(), "name").get_str() == aliasname);
	BOOST_CHECK(find_value(r.get_obj(), "value").get_str() == aliasdata);
	BOOST_CHECK(find_value(r.get_obj(), "isaliasmine").get_bool() == true);
	r = CallRPC("node2", "aliasinfo " + aliasname);
	BOOST_CHECK(!r.isNull());
	BOOST_CHECK(find_value(r.get_obj(), "name").get_str() == aliasname);
	BOOST_CHECK(find_value(r.get_obj(), "value").get_str() == aliasdata);
	BOOST_CHECK(find_value(r.get_obj(), "isaliasmine").get_bool() == false);
	r = CallRPC("node3", "aliasinfo " + aliasname);
	BOOST_CHECK(!r.isNull());
	BOOST_CHECK(find_value(r.get_obj(), "name").get_str() == aliasname);
	BOOST_CHECK(find_value(r.get_obj(), "value").get_str() == aliasdata);
	BOOST_CHECK(find_value(r.get_obj(), "isaliasmine").get_bool() == false);
}
void AliasNewDataTooBig(const string& aliasname, const string& aliasdata)
{
	UniValue r;
	r = CallRPC("node1", "aliasnew " + aliasname + " " + aliasdata);
	GenerateBlocks(1);
	r = CallRPC("node1", "aliasinfo " + aliasname);
	BOOST_CHECK(r.isNull());
}
BasicSyscoinTestingSetup::BasicSyscoinTestingSetup()
{
}
BasicSyscoinTestingSetup::~BasicSyscoinTestingSetup()
{
}
SyscoinTestingSetup::SyscoinTestingSetup()
{
	Begin();
}
SyscoinTestingSetup::~SyscoinTestingSetup()
{
	End();
}
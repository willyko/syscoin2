#include "test_syscoin_services.h"
#include "utiltime.h"
#include "util.h"



#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/thread.hpp>
#include <boost/lexical_cast.hpp>

// SYSCOIN testing setup
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
	try{
		CallRPC("node1", "stop");
	}
	catch(const runtime_error& error)
	{
	}	
	MilliSleep(3000);
	printf("Stopping node2..\n");
	try{
		CallRPC("node2", "stop");
	}
	catch(const runtime_error& error)
	{
	}	
	MilliSleep(3000);
	printf("Stopping node3..\n");
	try{
		CallRPC("node3", "stop");
	}
	catch(const runtime_error& error)
	{
	}	
	MilliSleep(3000);
	printf("Done!\n");
}
void StartNode(const string &dataDir)
{
    boost::filesystem::path fpath = boost::filesystem::system_complete("../syscoind");
	string nodePath = fpath.string() + string(" -datadir=") + dataDir + string(" -regtest");
    boost::thread t(runCommand, nodePath);
	printf("Launching %s, waiting 3 seconds before trying to ping...\n", dataDir.c_str());
	MilliSleep(3000);
	while (1)
	{
		try{
			CallRPC(dataDir, "getinfo");
		}
		catch(const runtime_error& error)
		{
			printf("Waiting for %s to come online, trying again in 5 seconds...\n", dataDir.c_str());
			MilliSleep(5000);
			continue;
		}
		break;
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
	if(val.isNull())
		throw runtime_error("Could not parse rpc results");
	return val;
}
std::string CallExternal(std::string &cmd)
{
	printf("open\n");
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
  int height, newHeight, timeoutCounter;
  UniValue r;
  BOOST_CHECK_NO_THROW(r = CallRPC("node1", "getinfo"));
  newHeight = find_value(r.get_obj(), "blocks").get_int() + nBlocks;

  BOOST_CHECK_NO_THROW(r = CallRPC("node1", "generate " + boost::lexical_cast<std::string>(nBlocks)));
  BOOST_CHECK_NO_THROW(r = CallRPC("node1", "getinfo"));
  height = find_value(r.get_obj(), "blocks").get_int();
  BOOST_CHECK(height == newHeight);
  height = 0;
  timeoutCounter = 0;
  while(height != newHeight)
  {
	  MilliSleep(100);
	  BOOST_CHECK_NO_THROW(r = CallRPC("node2", "getinfo"));
	  height = find_value(r.get_obj(), "blocks").get_int();
	  timeoutCounter++;
	  if(timeoutCounter > 100)
		  break;
  }
  BOOST_CHECK(height == newHeight);
  height = 0;
  timeoutCounter = 0;
  while(height != newHeight)
  {
	  MilliSleep(100);
	  BOOST_CHECK_NO_THROW(r = CallRPC("node3", "getinfo"));
	  height = find_value(r.get_obj(), "blocks").get_int();
	  timeoutCounter++;
	  if(timeoutCounter > 100)
		  break;
  }
  BOOST_CHECK(height == newHeight);
  height = 0;
  timeoutCounter = 0;
}
void AliasNew(const string& node, const string& aliasname, const string& aliasdata)
{
	string otherNode1 = "node2";
	string otherNode2 = "node3";
	if(node == "node2")
	{
		otherNode1 = "node3";
		otherNode2 = "node1";
	}
	else if(node == "node3")
	{
		otherNode1 = "node1";
		otherNode2 = "node2";
	}
	UniValue r;
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "aliasnew " + aliasname + " " + aliasdata));
	GenerateBlocks(1);
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "aliasinfo " + aliasname));
	BOOST_CHECK(find_value(r.get_obj(), "name").get_str() == aliasname);
	BOOST_CHECK(find_value(r.get_obj(), "value").get_str() == aliasdata);
	BOOST_CHECK(find_value(r.get_obj(), "isaliasmine").get_bool() == true);
	BOOST_CHECK_NO_THROW(r = CallRPC(otherNode1, "aliasinfo " + aliasname));
	BOOST_CHECK(find_value(r.get_obj(), "name").get_str() == aliasname);
	BOOST_CHECK(find_value(r.get_obj(), "value").get_str() == aliasdata);
	BOOST_CHECK(find_value(r.get_obj(), "isaliasmine").get_bool() == false);
	BOOST_CHECK_NO_THROW(r = CallRPC(otherNode2, "aliasinfo " + aliasname));
	BOOST_CHECK(find_value(r.get_obj(), "name").get_str() == aliasname);
	BOOST_CHECK(find_value(r.get_obj(), "value").get_str() == aliasdata);
	BOOST_CHECK(find_value(r.get_obj(), "isaliasmine").get_bool() == false);
}
void AliasUpdate(const string& node, const string& aliasname, const string& aliasdata)
{
	string otherNode1 = "node2";
	string otherNode2 = "node3";
	if(node == "node2")
	{
		otherNode1 = "node3";
		otherNode2 = "node1";
	}
	else if(node == "node3")
	{
		otherNode1 = "node1";
		otherNode2 = "node2";
	}
	UniValue r;
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "aliasupdate " + aliasname + " " + aliasdata));
	GenerateBlocks(1);
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "aliasinfo " + aliasname));
	BOOST_CHECK(find_value(r.get_obj(), "name").get_str() == aliasname);
	BOOST_CHECK(find_value(r.get_obj(), "value").get_str() == aliasdata);
	BOOST_CHECK(find_value(r.get_obj(), "isaliasmine").get_bool() == true);
	BOOST_CHECK_NO_THROW(r = CallRPC(otherNode1, "aliasinfo " + aliasname));
	BOOST_CHECK(find_value(r.get_obj(), "name").get_str() == aliasname);
	BOOST_CHECK(find_value(r.get_obj(), "value").get_str() == aliasdata);
	BOOST_CHECK(find_value(r.get_obj(), "isaliasmine").get_bool() == false);
	BOOST_CHECK_NO_THROW(r = CallRPC(otherNode2, "aliasinfo " + aliasname));
	BOOST_CHECK(find_value(r.get_obj(), "name").get_str() == aliasname);
	BOOST_CHECK(find_value(r.get_obj(), "value").get_str() == aliasdata);
	BOOST_CHECK(find_value(r.get_obj(), "isaliasmine").get_bool() == false);
}
BasicSyscoinTestingSetup::BasicSyscoinTestingSetup()
{
}
BasicSyscoinTestingSetup::~BasicSyscoinTestingSetup()
{
}
SyscoinTestingSetup::SyscoinTestingSetup()
{
	StartNodes();
}
SyscoinTestingSetup::~SyscoinTestingSetup()
{
	StopNodes();
	if(boost::filesystem::exists(boost::filesystem::system_complete("node1/regtest")))
		boost::filesystem::remove_all(boost::filesystem::system_complete("node1/regtest"));
	MilliSleep(1000);
	if(boost::filesystem::exists(boost::filesystem::system_complete("node2/regtest")))
		boost::filesystem::remove_all(boost::filesystem::system_complete("node2/regtest"));
	MilliSleep(1000);
	if(boost::filesystem::exists(boost::filesystem::system_complete("node3/regtest")))
		boost::filesystem::remove_all(boost::filesystem::system_complete("node3/regtest"));
}
#ifndef SYSCOIN_TEST_TEST_SYSCOIN_SERVICES_H
#define SYSCOIN_TEST_TEST_SYSCOIN_SERVICES_H

#include <stdio.h>
#include <univalue.h>
using namespace std;
/** Testing syscoin services setup that configures a complete environment with 3 nodes.
 */
// SYSCOIN testing setup
struct SyscoinTestingSetup {
    SyscoinTestingSetup();
    ~SyscoinTestingSetup();
	string CallExternal(string &cmd);
	UniValue CallRPC(const string &dataDir, const string& commandWithArgs);
	void StartNode(const string &dataDir);
	void GenerateBlocks(int nBlocks);
	void AliasNew(const string& aliasname, const string& aliasdata);
	void AliasNewTooBig(const string& aliasname, const string& aliasdata);
};

#endif

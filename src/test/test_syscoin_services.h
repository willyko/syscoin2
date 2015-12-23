#ifndef SYSCOIN_TEST_TEST_SYSCOIN_SERVICES_H
#define SYSCOIN_TEST_TEST_SYSCOIN_SERVICES_H

#include <stdio.h>
#include <univalue.h>
using namespace std;
/** Testing syscoin services setup that configures a complete environment with 3 nodes.
 */
UniValue CallRPC(const string &dataDir, const string& commandWithArgs);
void StartNode(const string &dataDir);
void StartNodes();
void StopNodes();
void GenerateBlocks(int nBlocks);
string CallExternal(string &cmd);
void AliasNew(const string& aliasname, const string& aliasdata);
void AliasNewTooBig(const string& aliasname, const string& aliasdata);
// SYSCOIN testing setup
struct SyscoinTestingSetup {
    SyscoinTestingSetup();
    ~SyscoinTestingSetup();
};

#endif

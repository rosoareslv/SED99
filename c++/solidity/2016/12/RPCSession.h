/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file RPCSession.h
 * @author Dimtiry Khokhlov <dimitry@ethdev.com>
 * @date 2016
 */

#if defined(_WIN32)
#include <windows.h>
#include "libdevcore/UndefMacros.h"
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#endif

#include <string>
#include <stdio.h>
#include <map>
#include <json/value.h>
#include <boost/test/unit_test.hpp>

#if defined(_WIN32)
const int c_buffsize = 5120000; //because windows pipe is broken and wont work as in examples. use larger buffer limit to receive whole package in one call
class IPCSocket : public boost::noncopyable
{
public:
	IPCSocket(std::string const& _path);
	std::string sendRequest(std::string const& _req);
	~IPCSocket() { CloseHandle(m_socket); }

	std::string const& path() const { return m_path; }

private:
	std::string m_path;
	HANDLE  m_socket;
};
#else
class IPCSocket: public boost::noncopyable
{
public:
	IPCSocket(std::string const& _path);
	std::string sendRequest(std::string const& _req);
	~IPCSocket() { close(m_socket); fclose(m_fp); }

	std::string const& path() const { return m_path; }

private:
	FILE *m_fp;
	std::string m_path;
	int m_socket;
};
#endif

class RPCSession: public boost::noncopyable
{
public:
	struct TransactionData
	{
		std::string from;
		std::string to;
		std::string gas;
		std::string gasPrice;
		std::string value;
		std::string data;

		std::string toJson() const;
	};

	struct LogEntry {
		std::string address;
		std::vector<std::string> topics;
		std::string data;
	};

	struct TransactionReceipt
	{
		std::string gasUsed;
		std::string contractAddress;
		std::vector<LogEntry> logEntries;
	};

	static RPCSession& instance(std::string const& _path);

	std::string eth_getCode(std::string const& _address, std::string const& _blockNumber);
	std::string eth_call(TransactionData const& _td, std::string const& _blockNumber);
	TransactionReceipt eth_getTransactionReceipt(std::string const& _transactionHash);
	std::string eth_sendTransaction(TransactionData const& _transactionData);
	std::string eth_sendTransaction(std::string const& _transaction);
	std::string eth_getBalance(std::string const& _address, std::string const& _blockNumber);
	std::string eth_getStorageRoot(std::string const& _address, std::string const& _blockNumber);
	std::string personal_newAccount(std::string const& _password);
	void personal_unlockAccount(std::string const& _address, std::string const& _password, int _duration);
	void test_setChainParams(std::vector<std::string> const& _accounts);
	void test_setChainParams(std::string const& _config);
	void test_rewindToBlock(size_t _blockNr);
	void test_modifyTimestamp(size_t _timestamp);
	void test_mineBlocks(int _number);
	Json::Value rpcCall(std::string const& _methodName, std::vector<std::string> const& _args = std::vector<std::string>(), bool _canFail = false);

	std::string const& account(size_t _id) const { return m_accounts.at(_id); }
	std::string const& accountCreateIfNotExists(size_t _id);

private:
	RPCSession(std::string const& _path);

	inline std::string quote(std::string const& _arg) { return "\"" + _arg + "\""; }
	/// Parse std::string replacing keywords to values
	void parseString(std::string& _string, std::map<std::string, std::string> const& _varMap);

	IPCSocket m_ipcSocket;
	size_t m_rpcSequence = 1;
	unsigned m_sleepTime = 10;
	unsigned m_successfulMineRuns = 0;

	std::vector<std::string> m_accounts;
};


#include "core/io/multiplayer_api.h"
#include "core/io/marshalls.h"
#include "scene/main/node.h"

void MultiplayerAPI::poll() {

	if (!network_peer.is_valid() || network_peer->get_connection_status() == NetworkedMultiplayerPeer::CONNECTION_DISCONNECTED)
		return;

	network_peer->poll();

	if (!network_peer.is_valid()) //it's possible that polling might have resulted in a disconnection, so check here
		return;

	while (network_peer->get_available_packet_count()) {

		int sender = network_peer->get_packet_peer();
		const uint8_t *packet;
		int len;

		Error err = network_peer->get_packet(&packet, len);
		if (err != OK) {
			ERR_PRINT("Error getting packet!");
		}

		rpc_sender_id = sender;
		_process_packet(sender, packet, len);
		rpc_sender_id = 0;

		if (!network_peer.is_valid()) {
			break; //it's also possible that a packet or RPC caused a disconnection, so also check here
		}
	}
}

void MultiplayerAPI::clear() {
	connected_peers.clear();
	path_get_cache.clear();
	path_send_cache.clear();
	last_send_cache_id = 1;
}

void MultiplayerAPI::set_root_node(Node *p_node) {
	root_node = p_node;
}

void MultiplayerAPI::set_network_peer(const Ref<NetworkedMultiplayerPeer> &p_peer) {

	if (network_peer.is_valid()) {
		network_peer->disconnect("peer_connected", this, "add_peer");
		network_peer->disconnect("peer_disconnected", this, "del_peer");
		network_peer->disconnect("connection_succeeded", this, "connected_to_server");
		network_peer->disconnect("connection_failed", this, "connection_failed");
		network_peer->disconnect("server_disconnected", this, "server_disconnected");
		clear();
	}

	network_peer = p_peer;

	ERR_EXPLAIN("Supplied NetworkedNetworkPeer must be connecting or connected.");
	ERR_FAIL_COND(p_peer.is_valid() && p_peer->get_connection_status() == NetworkedMultiplayerPeer::CONNECTION_DISCONNECTED);

	if (network_peer.is_valid()) {
		network_peer->connect("peer_connected", this, "add_peer");
		network_peer->connect("peer_disconnected", this, "del_peer");
		network_peer->connect("connection_succeeded", this, "connected_to_server");
		network_peer->connect("connection_failed", this, "connection_failed");
		network_peer->connect("server_disconnected", this, "server_disconnected");
	}
}

Ref<NetworkedMultiplayerPeer> MultiplayerAPI::get_network_peer() const {
	return network_peer;
}

void MultiplayerAPI::_process_packet(int p_from, const uint8_t *p_packet, int p_packet_len) {

	ERR_FAIL_COND(root_node == NULL);
	ERR_FAIL_COND(p_packet_len < 5);

	uint8_t packet_type = p_packet[0];

	switch (packet_type) {

		case NETWORK_COMMAND_SIMPLIFY_PATH: {

			_process_simplify_path(p_from, p_packet, p_packet_len);
		} break;

		case NETWORK_COMMAND_CONFIRM_PATH: {

			_process_confirm_path(p_from, p_packet, p_packet_len);
		} break;

		case NETWORK_COMMAND_REMOTE_CALL:
		case NETWORK_COMMAND_REMOTE_SET: {

			ERR_FAIL_COND(p_packet_len < 6);

			Node *node = _process_get_node(p_from, p_packet, p_packet_len);

			ERR_FAIL_COND(node == NULL);

			//detect cstring end
			int len_end = 5;
			for (; len_end < p_packet_len; len_end++) {
				if (p_packet[len_end] == 0) {
					break;
				}
			}

			ERR_FAIL_COND(len_end >= p_packet_len);

			StringName name = String::utf8((const char *)&p_packet[5]);

			if (packet_type == NETWORK_COMMAND_REMOTE_CALL) {

				_process_rpc(node, name, p_from, p_packet, p_packet_len, len_end + 1);

			} else {

				_process_rset(node, name, p_from, p_packet, p_packet_len, len_end + 1);
			}

		} break;
	}
}

Node *MultiplayerAPI::_process_get_node(int p_from, const uint8_t *p_packet, int p_packet_len) {

	uint32_t target = decode_uint32(&p_packet[1]);
	Node *node = NULL;

	if (target & 0x80000000) {
		//use full path (not cached yet)

		int ofs = target & 0x7FFFFFFF;
		ERR_FAIL_COND_V(ofs >= p_packet_len, NULL);

		String paths;
		paths.parse_utf8((const char *)&p_packet[ofs], p_packet_len - ofs);

		NodePath np = paths;

		node = root_node->get_node(np);

		if (!node)
			ERR_PRINTS("Failed to get path from RPC: " + String(np));
	} else {
		//use cached path
		int id = target;

		Map<int, PathGetCache>::Element *E = path_get_cache.find(p_from);
		ERR_FAIL_COND_V(!E, NULL);

		Map<int, PathGetCache::NodeInfo>::Element *F = E->get().nodes.find(id);
		ERR_FAIL_COND_V(!F, NULL);

		PathGetCache::NodeInfo *ni = &F->get();
		//do proper caching later

		node = root_node->get_node(ni->path);
		if (!node)
			ERR_PRINTS("Failed to get cached path from RPC: " + String(ni->path));
	}
	return node;
}

void MultiplayerAPI::_process_rpc(Node *p_node, const StringName &p_name, int p_from, const uint8_t *p_packet, int p_packet_len, int p_offset) {
	if (!p_node->can_call_rpc(p_name, p_from))
		return;

	ERR_FAIL_COND(p_offset >= p_packet_len);

	int argc = p_packet[p_offset];
	Vector<Variant> args;
	Vector<const Variant *> argp;
	args.resize(argc);
	argp.resize(argc);

	p_offset++;

	for (int i = 0; i < argc; i++) {

		ERR_FAIL_COND(p_offset >= p_packet_len);
		int vlen;
		Error err = decode_variant(args[i], &p_packet[p_offset], p_packet_len - p_offset, &vlen);
		ERR_FAIL_COND(err != OK);
		//args[i]=p_packet[3+i];
		argp[i] = &args[i];
		p_offset += vlen;
	}

	Variant::CallError ce;

	p_node->call(p_name, (const Variant **)argp.ptr(), argc, ce);
	if (ce.error != Variant::CallError::CALL_OK) {
		String error = Variant::get_call_error_text(p_node, p_name, (const Variant **)argp.ptr(), argc, ce);
		error = "RPC - " + error;
		ERR_PRINTS(error);
	}
}

void MultiplayerAPI::_process_rset(Node *p_node, const StringName &p_name, int p_from, const uint8_t *p_packet, int p_packet_len, int p_offset) {

	if (!p_node->can_call_rset(p_name, p_from))
		return;

	ERR_FAIL_COND(p_offset >= p_packet_len);

	Variant value;
	decode_variant(value, &p_packet[p_offset], p_packet_len - p_offset);

	bool valid;

	p_node->set(p_name, value, &valid);
	if (!valid) {
		String error = "Error setting remote property '" + String(p_name) + "', not found in object of type " + p_node->get_class();
		ERR_PRINTS(error);
	}
}

void MultiplayerAPI::_process_simplify_path(int p_from, const uint8_t *p_packet, int p_packet_len) {

	ERR_FAIL_COND(p_packet_len < 5);
	int id = decode_uint32(&p_packet[1]);

	String paths;
	paths.parse_utf8((const char *)&p_packet[5], p_packet_len - 5);

	NodePath path = paths;

	if (!path_get_cache.has(p_from)) {
		path_get_cache[p_from] = PathGetCache();
	}

	PathGetCache::NodeInfo ni;
	ni.path = path;
	ni.instance = 0;

	path_get_cache[p_from].nodes[id] = ni;

	//send ack

	//encode path
	CharString pname = String(path).utf8();
	int len = encode_cstring(pname.get_data(), NULL);

	Vector<uint8_t> packet;

	packet.resize(1 + len);
	packet[0] = NETWORK_COMMAND_CONFIRM_PATH;
	encode_cstring(pname.get_data(), &packet[1]);

	network_peer->set_transfer_mode(NetworkedMultiplayerPeer::TRANSFER_MODE_RELIABLE);
	network_peer->set_target_peer(p_from);
	network_peer->put_packet(packet.ptr(), packet.size());
}

void MultiplayerAPI::_process_confirm_path(int p_from, const uint8_t *p_packet, int p_packet_len) {

	String paths;
	paths.parse_utf8((const char *)&p_packet[1], p_packet_len - 1);

	NodePath path = paths;

	PathSentCache *psc = path_send_cache.getptr(path);
	ERR_FAIL_COND(!psc);

	Map<int, bool>::Element *E = psc->confirmed_peers.find(p_from);
	ERR_FAIL_COND(!E);
	E->get() = true;
}

bool MultiplayerAPI::_send_confirm_path(NodePath p_path, PathSentCache *psc, int p_target) {
	bool has_all_peers = true;
	List<int> peers_to_add; //if one is missing, take note to add it

	for (Set<int>::Element *E = connected_peers.front(); E; E = E->next()) {

		if (p_target < 0 && E->get() == -p_target)
			continue; //continue, excluded

		if (p_target > 0 && E->get() != p_target)
			continue; //continue, not for this peer

		Map<int, bool>::Element *F = psc->confirmed_peers.find(E->get());

		if (!F || F->get() == false) {
			//path was not cached, or was cached but is unconfirmed
			if (!F) {
				//not cached at all, take note
				peers_to_add.push_back(E->get());
			}

			has_all_peers = false;
		}
	}

	//those that need to be added, send a message for this

	for (List<int>::Element *E = peers_to_add.front(); E; E = E->next()) {

		//encode function name
		CharString pname = String(p_path).utf8();
		int len = encode_cstring(pname.get_data(), NULL);

		Vector<uint8_t> packet;

		packet.resize(1 + 4 + len);
		packet[0] = NETWORK_COMMAND_SIMPLIFY_PATH;
		encode_uint32(psc->id, &packet[1]);
		encode_cstring(pname.get_data(), &packet[5]);

		network_peer->set_target_peer(E->get()); //to all of you
		network_peer->set_transfer_mode(NetworkedMultiplayerPeer::TRANSFER_MODE_RELIABLE);
		network_peer->put_packet(packet.ptr(), packet.size());

		psc->confirmed_peers.insert(E->get(), false); //insert into confirmed, but as false since it was not confirmed
	}

	return has_all_peers;
}

void MultiplayerAPI::_send_rpc(Node *p_from, int p_to, bool p_unreliable, bool p_set, const StringName &p_name, const Variant **p_arg, int p_argcount) {

	if (network_peer.is_null()) {
		ERR_EXPLAIN("Attempt to remote call/set when networking is not active in SceneTree.");
		ERR_FAIL();
	}

	if (network_peer->get_connection_status() == NetworkedMultiplayerPeer::CONNECTION_CONNECTING) {
		ERR_EXPLAIN("Attempt to remote call/set when networking is not connected yet in SceneTree.");
		ERR_FAIL();
	}

	if (network_peer->get_connection_status() == NetworkedMultiplayerPeer::CONNECTION_DISCONNECTED) {
		ERR_EXPLAIN("Attempt to remote call/set when networking is disconnected.");
		ERR_FAIL();
	}

	if (p_argcount > 255) {
		ERR_EXPLAIN("Too many arguments >255.");
		ERR_FAIL();
	}

	if (p_to != 0 && !connected_peers.has(ABS(p_to))) {
		if (p_to == network_peer->get_unique_id()) {
			ERR_EXPLAIN("Attempt to remote call/set yourself! unique ID: " + itos(network_peer->get_unique_id()));
		} else {
			ERR_EXPLAIN("Attempt to remote call unexisting ID: " + itos(p_to));
		}

		ERR_FAIL();
	}

	NodePath from_path = (root_node->get_path()).rel_path_to(p_from->get_path());
	ERR_FAIL_COND(from_path.is_empty());

	//see if the path is cached
	PathSentCache *psc = path_send_cache.getptr(from_path);
	if (!psc) {
		//path is not cached, create
		path_send_cache[from_path] = PathSentCache();
		psc = path_send_cache.getptr(from_path);
		psc->id = last_send_cache_id++;
	}

	//create base packet, lots of hardcode because it must be tight

	int ofs = 0;

#define MAKE_ROOM(m_amount) \
	if (packet_cache.size() < m_amount) packet_cache.resize(m_amount);

	//encode type
	MAKE_ROOM(1);
	packet_cache[0] = p_set ? NETWORK_COMMAND_REMOTE_SET : NETWORK_COMMAND_REMOTE_CALL;
	ofs += 1;

	//encode ID
	MAKE_ROOM(ofs + 4);
	encode_uint32(psc->id, &(packet_cache[ofs]));
	ofs += 4;

	//encode function name
	CharString name = String(p_name).utf8();
	int len = encode_cstring(name.get_data(), NULL);
	MAKE_ROOM(ofs + len);
	encode_cstring(name.get_data(), &(packet_cache[ofs]));
	ofs += len;

	if (p_set) {
		//set argument
		Error err = encode_variant(*p_arg[0], NULL, len);
		ERR_FAIL_COND(err != OK);
		MAKE_ROOM(ofs + len);
		encode_variant(*p_arg[0], &(packet_cache[ofs]), len);
		ofs += len;

	} else {
		//call arguments
		MAKE_ROOM(ofs + 1);
		packet_cache[ofs] = p_argcount;
		ofs += 1;
		for (int i = 0; i < p_argcount; i++) {
			Error err = encode_variant(*p_arg[i], NULL, len);
			ERR_FAIL_COND(err != OK);
			MAKE_ROOM(ofs + len);
			encode_variant(*p_arg[i], &(packet_cache[ofs]), len);
			ofs += len;
		}
	}

	//see if all peers have cached path (is so, call can be fast)
	bool has_all_peers = _send_confirm_path(from_path, psc, p_to);

	//take chance and set transfer mode, since all send methods will use it
	network_peer->set_transfer_mode(p_unreliable ? NetworkedMultiplayerPeer::TRANSFER_MODE_UNRELIABLE : NetworkedMultiplayerPeer::TRANSFER_MODE_RELIABLE);

	if (has_all_peers) {

		//they all have verified paths, so send fast
		network_peer->set_target_peer(p_to); //to all of you
		network_peer->put_packet(packet_cache.ptr(), ofs); //a message with love
	} else {
		//not all verified path, so send one by one

		//apend path at the end, since we will need it for some packets
		CharString pname = String(from_path).utf8();
		int path_len = encode_cstring(pname.get_data(), NULL);
		MAKE_ROOM(ofs + path_len);
		encode_cstring(pname.get_data(), &(packet_cache[ofs]));

		for (Set<int>::Element *E = connected_peers.front(); E; E = E->next()) {

			if (p_to < 0 && E->get() == -p_to)
				continue; //continue, excluded

			if (p_to > 0 && E->get() != p_to)
				continue; //continue, not for this peer

			Map<int, bool>::Element *F = psc->confirmed_peers.find(E->get());
			ERR_CONTINUE(!F); //should never happen

			network_peer->set_target_peer(E->get()); //to this one specifically

			if (F->get() == true) {
				//this one confirmed path, so use id
				encode_uint32(psc->id, &(packet_cache[1]));
				network_peer->put_packet(packet_cache.ptr(), ofs);
			} else {
				//this one did not confirm path yet, so use entire path (sorry!)
				encode_uint32(0x80000000 | ofs, &(packet_cache[1])); //offset to path and flag
				network_peer->put_packet(packet_cache.ptr(), ofs + path_len);
			}
		}
	}
}

void MultiplayerAPI::add_peer(int p_id) {
	connected_peers.insert(p_id);
	path_get_cache.insert(p_id, PathGetCache());
	emit_signal("network_peer_connected", p_id);
}

void MultiplayerAPI::del_peer(int p_id) {
	connected_peers.erase(p_id);
	path_get_cache.erase(p_id); //I no longer need your cache, sorry
	emit_signal("network_peer_disconnected", p_id);
}

void MultiplayerAPI::connected_to_server() {

	emit_signal("connected_to_server");
}

void MultiplayerAPI::connection_failed() {

	emit_signal("connection_failed");
}

void MultiplayerAPI::server_disconnected() {

	emit_signal("server_disconnected");
}

bool _should_call_native(Node::RPCMode mode, bool is_master, bool &r_skip_rpc) {

	switch (mode) {

		case Node::RPC_MODE_DISABLED: {
			//do nothing
		} break;
		case Node::RPC_MODE_REMOTE: {
			//do nothing also, no need to call local
		} break;
		case Node::RPC_MODE_SYNC: {
			//call it, sync always results in call
			return true;
		} break;
		case Node::RPC_MODE_MASTER: {
			if (is_master)
				r_skip_rpc = true; //no other master so..
			return is_master;
		} break;
		case Node::RPC_MODE_SLAVE: {
			return !is_master;
		} break;
	}
	return false;
}

bool _should_call_script(ScriptInstance::RPCMode mode, bool is_master, bool &r_skip_rpc) {
	switch (mode) {

		case ScriptInstance::RPC_MODE_DISABLED: {
			//do nothing
		} break;
		case ScriptInstance::RPC_MODE_REMOTE: {
			//do nothing also, no need to call local
		} break;
		case ScriptInstance::RPC_MODE_SYNC: {
			//call it, sync always results in call
			return true;
		} break;
		case ScriptInstance::RPC_MODE_MASTER: {
			if (is_master)
				r_skip_rpc = true; //no other master so..
			return is_master;
		} break;
		case ScriptInstance::RPC_MODE_SLAVE: {
			return !is_master;
		} break;
	}
	return false;
}

void MultiplayerAPI::rpcp(Node *p_node, int p_peer_id, bool p_unreliable, const StringName &p_method, const Variant **p_arg, int p_argcount) {

	ERR_FAIL_COND(!p_node->is_inside_tree());
	ERR_FAIL_COND(!network_peer.is_valid());

	int node_id = network_peer->get_unique_id();
	bool skip_rpc = false;
	bool call_local_native = false;
	bool call_local_script = false;
	bool is_master = p_node->is_network_master();

	if (p_peer_id == 0 || p_peer_id == node_id || (p_peer_id < 0 && p_peer_id != -node_id)) {
		//check that send mode can use local call

		const Map<StringName, Node::RPCMode>::Element *E = p_node->get_node_rpc_mode(p_method);
		if (E) {
			call_local_native = _should_call_native(E->get(), is_master, skip_rpc);
		}

		if (call_local_native) {
			// done below
		} else if (p_node->get_script_instance()) {
			//attempt with script
			ScriptInstance::RPCMode rpc_mode = p_node->get_script_instance()->get_rpc_mode(p_method);
			call_local_script = _should_call_script(rpc_mode, is_master, skip_rpc);
		}
	}

	if (!skip_rpc) {
		_send_rpc(p_node, p_peer_id, p_unreliable, false, p_method, p_arg, p_argcount);
	}

	if (call_local_native) {
		Variant::CallError ce;
		p_node->call(p_method, p_arg, p_argcount, ce);
		if (ce.error != Variant::CallError::CALL_OK) {
			String error = Variant::get_call_error_text(p_node, p_method, p_arg, p_argcount, ce);
			error = "rpc() aborted in local call:  - " + error;
			ERR_PRINTS(error);
			return;
		}
	}

	if (call_local_script) {
		Variant::CallError ce;
		ce.error = Variant::CallError::CALL_OK;
		p_node->get_script_instance()->call(p_method, p_arg, p_argcount, ce);
		if (ce.error != Variant::CallError::CALL_OK) {
			String error = Variant::get_call_error_text(p_node, p_method, p_arg, p_argcount, ce);
			error = "rpc() aborted in script local call:  - " + error;
			ERR_PRINTS(error);
			return;
		}
	}
}

void MultiplayerAPI::rsetp(Node *p_node, int p_peer_id, bool p_unreliable, const StringName &p_property, const Variant &p_value) {

	ERR_FAIL_COND(!p_node->is_inside_tree());
	ERR_FAIL_COND(!network_peer.is_valid());

	int node_id = network_peer->get_unique_id();
	bool is_master = p_node->is_network_master();
	bool skip_rset = false;

	if (p_peer_id == 0 || p_peer_id == node_id || (p_peer_id < 0 && p_peer_id != -node_id)) {
		//check that send mode can use local call

		bool set_local = false;

		const Map<StringName, Node::RPCMode>::Element *E = p_node->get_node_rset_mode(p_property);
		if (E) {

			set_local = _should_call_native(E->get(), is_master, skip_rset);
		}

		if (set_local) {
			bool valid;
			p_node->set(p_property, p_value, &valid);

			if (!valid) {
				String error = "rset() aborted in local set, property not found:  - " + String(p_property);
				ERR_PRINTS(error);
				return;
			}
		} else if (p_node->get_script_instance()) {
			//attempt with script
			ScriptInstance::RPCMode rpc_mode = p_node->get_script_instance()->get_rset_mode(p_property);

			set_local = _should_call_script(rpc_mode, is_master, skip_rset);

			if (set_local) {

				bool valid = p_node->get_script_instance()->set(p_property, p_value);

				if (!valid) {
					String error = "rset() aborted in local script set, property not found:  - " + String(p_property);
					ERR_PRINTS(error);
					return;
				}
			}
		}
	}

	if (skip_rset)
		return;

	const Variant *vptr = &p_value;

	_send_rpc(p_node, p_peer_id, p_unreliable, true, p_property, &vptr, 1);
}

int MultiplayerAPI::get_network_unique_id() const {

	ERR_FAIL_COND_V(!network_peer.is_valid(), 0);
	return network_peer->get_unique_id();
}

bool MultiplayerAPI::is_network_server() const {

	ERR_FAIL_COND_V(!network_peer.is_valid(), false);
	return network_peer->is_server();
}

void MultiplayerAPI::set_refuse_new_network_connections(bool p_refuse) {

	ERR_FAIL_COND(!network_peer.is_valid());
	network_peer->set_refuse_new_connections(p_refuse);
}

bool MultiplayerAPI::is_refusing_new_network_connections() const {

	ERR_FAIL_COND_V(!network_peer.is_valid(), false);
	return network_peer->is_refusing_new_connections();
}

Vector<int> MultiplayerAPI::get_network_connected_peers() const {

	ERR_FAIL_COND_V(!network_peer.is_valid(), Vector<int>());

	Vector<int> ret;
	for (Set<int>::Element *E = connected_peers.front(); E; E = E->next()) {
		ret.push_back(E->get());
	}

	return ret;
}

void MultiplayerAPI::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_root_node", "node"), &MultiplayerAPI::set_root_node);
	ClassDB::bind_method(D_METHOD("has_network_peer"), &MultiplayerAPI::has_network_peer);
	ClassDB::bind_method(D_METHOD("get_network_peer"), &MultiplayerAPI::get_network_peer);
	ClassDB::bind_method(D_METHOD("get_network_unique_id"), &MultiplayerAPI::get_network_unique_id);
	ClassDB::bind_method(D_METHOD("is_network_server"), &MultiplayerAPI::is_network_server);
	ClassDB::bind_method(D_METHOD("get_rpc_sender_id"), &MultiplayerAPI::get_rpc_sender_id);
	ClassDB::bind_method(D_METHOD("add_peer", "id"), &MultiplayerAPI::add_peer);
	ClassDB::bind_method(D_METHOD("del_peer", "id"), &MultiplayerAPI::del_peer);
	ClassDB::bind_method(D_METHOD("set_network_peer", "peer"), &MultiplayerAPI::set_network_peer);
	ClassDB::bind_method(D_METHOD("poll"), &MultiplayerAPI::poll);
	ClassDB::bind_method(D_METHOD("clear"), &MultiplayerAPI::clear);

	ClassDB::bind_method(D_METHOD("connected_to_server"), &MultiplayerAPI::connected_to_server);
	ClassDB::bind_method(D_METHOD("connection_failed"), &MultiplayerAPI::connection_failed);
	ClassDB::bind_method(D_METHOD("server_disconnected"), &MultiplayerAPI::server_disconnected);
	ClassDB::bind_method(D_METHOD("get_network_connected_peers"), &MultiplayerAPI::get_network_connected_peers);
	ClassDB::bind_method(D_METHOD("set_refuse_new_network_connections", "refuse"), &MultiplayerAPI::set_refuse_new_network_connections);
	ClassDB::bind_method(D_METHOD("is_refusing_new_network_connections"), &MultiplayerAPI::is_refusing_new_network_connections);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "refuse_new_network_connections"), "set_refuse_new_network_connections", "is_refusing_new_network_connections");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "network_peer", PROPERTY_HINT_RESOURCE_TYPE, "NetworkedMultiplayerPeer", 0), "set_network_peer", "get_network_peer");

	ADD_SIGNAL(MethodInfo("network_peer_connected", PropertyInfo(Variant::INT, "id")));
	ADD_SIGNAL(MethodInfo("network_peer_disconnected", PropertyInfo(Variant::INT, "id")));
	ADD_SIGNAL(MethodInfo("connected_to_server"));
	ADD_SIGNAL(MethodInfo("connection_failed"));
	ADD_SIGNAL(MethodInfo("server_disconnected"));
}

MultiplayerAPI::MultiplayerAPI() {
	clear();
}

MultiplayerAPI::~MultiplayerAPI() {
	clear();
}

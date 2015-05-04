//
// Security Manager
//
//    Eduard Grasa <eduard.grasa@i2cat.net>
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// 
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
// MA  02110-1301  USA
//

#define RINA_PREFIX "librina.security-manager"

#include "librina/logs.h"
#include "librina/security-manager.h"

namespace rina {

//Class AuthPolicySet
const std::string IAuthPolicySet::AUTH_NONE = "authentication-none";
const std::string IAuthPolicySet::AUTH_PASSWORD = "authentication-password";
const std::string IAuthPolicySet::AUTH_SSHRSA = "authentication-sshrsa";
const std::string IAuthPolicySet::AUTH_SSHDSA = "authentication-sshdsa";

IAuthPolicySet::IAuthPolicySet(CDAPMessage::AuthTypes type_)
{
	type = cdapTypeToString(type_);
}

const std::string IAuthPolicySet::cdapTypeToString(CDAPMessage::AuthTypes type)
{
	switch(type) {
	case CDAPMessage::AUTH_NONE:
		return AUTH_NONE;
	case CDAPMessage::AUTH_PASSWD:
		return AUTH_PASSWORD;
	case CDAPMessage::AUTH_SSHRSA:
		return AUTH_SSHRSA;
	case CDAPMessage::AUTH_SSHDSA:
		return AUTH_SSHDSA;
	default:
		throw Exception("Unknown authentication type");
	}
}

//Class AuthNonePolicySet
rina::AuthValue AuthNonePolicySet::get_my_credentials(int session_id)
{
	(void) session_id;

	rina::AuthValue result;
	return result;
}

rina::IAuthPolicySet::AuthStatus AuthNonePolicySet::initiate_authentication(rina::AuthValue credentials,
								      	    int session_id)
{
	(void) credentials;
	(void) session_id;

	return rina::IAuthPolicySet::SUCCESSFULL;
}

// No authentication messages exchanged
void AuthNonePolicySet::process_incoming_message(const CDAPMessage& message,
						 int session_id)
{
	(void) message;
	(void) session_id;
}

int AuthNonePolicySet::set_policy_set_param(const std::string& name,
                                            const std::string& value)
{
        LOG_DBG("No policy-set-specific parameters to set (%s, %s)",
                        name.c_str(), value.c_str());
        return -1;
}

//Class AuthPasswdPolicySet
const std::string AuthPasswordPolicySet::PASSWORD = "password";
const std::string AuthPasswordPolicySet::DEFAULT_CIPHER = "default";
const std::string AuthPasswordPolicySet::CHALLENGE_REQUEST = "challenge request";
const std::string AuthPasswordPolicySet::CHALLENGE_REPLY = "challenge reply";

// No credentials required, since the process being authenticated
// will have to demonstrate that it knows the password by encrypting
// a random challenge with a password string
rina::AuthValue AuthPasswordPolicySet::get_my_credentials(int session_id)
{
	(void) session_id;

	rina::AuthValue result;
	return result;
}

std::string * AuthPasswordPolicySet::generate_random_challenge()
{
	static const char alphanum[] =
			"0123456789"
			"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			"abcdefghijklmnopqrstuvwxyz";

	char *s = new char[challenge_length];

	for (int i = 0; i < challenge_length; ++i) {
		s[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
	}

	s[challenge_length] = 0;

	std::string * result = new std::string(s);
	delete s;

	return result;
}

std::string * AuthPasswordPolicySet::encrypt_challenge(const std::string& challenge)
{
	(void) challenge;
	return 0;
}

std::string * AuthPasswordPolicySet::decrypt_challenge(const std::string& encrypted_challenge)
{
	(void) encrypted_challenge;
	return 0;
}

rina::IAuthPolicySet::AuthStatus AuthPasswordPolicySet::initiate_authentication(rina::AuthValue credentials,
								      	    int session_id)
{
	(void) credentials;
	(void) session_id;

	WriteScopedLock writeLock(lock);

	//1 Generate a random password string and send it to the AP being authenticated
	std::string * challenge = generate_random_challenge();

	try {
		RIBObjectValue robject_value;
		robject_value.type_ = RIBObjectValue::stringtype;
		robject_value.string_value_ = *challenge;

		RemoteProcessId remote_id;
		remote_id.port_id_ = session_id;

		//object class contains challenge request or reply
		//object name contains cipher name
		rib_daemon->remoteWriteObject(CHALLENGE_REQUEST, cipher,
				robject_value, 0, remote_id, 0);
	} catch (Exception &e) {
		LOG_ERR("Problems encoding and sending CDAP message: %s", e.what());
	}

	//2 Store pending challenge and return
	pending_challenges.put(session_id, challenge);

	//3 TODO set timer to clean up pending authentication session upon timer expiry

	return rina::IAuthPolicySet::IN_PROGRESS;
}

void AuthPasswordPolicySet::authentication_failed(int session_id)
{
	//TODO clean up pending challenges and issue
	// an authentication failed notification
}

void AuthPasswordPolicySet::process_challenge_request(const CDAPMessage& message,
						      int session_id)
{
	//TODO
}

void AuthPasswordPolicySet::process_challenge_reply(const CDAPMessage& message,
						    int session_id)
{
	//TODO
}

void AuthPasswordPolicySet::process_incoming_message(const CDAPMessage& message,
						     int session_id)
{
	if (message.op_code_ != CDAPMessage::M_WRITE) {
		LOG_ERR("Wrong operation type");
		authentication_failed(session_id);
		return;
	}

	if (message.obj_value_ == 0) {
		LOG_ERR("Null object value");
		authentication_failed(session_id);
		return;
	}

	if (message.obj_class_ == CHALLENGE_REQUEST) {
		process_challenge_request(message, session_id);
	} else if (message.obj_class_ == CHALLENGE_REPLY) {
		process_challenge_reply(message, session_id);
	} else {
		LOG_ERR("Wrong message type");
		authentication_failed(session_id);
	}
}

// Allow modification of password via set_policy_set param
int AuthPasswordPolicySet::set_policy_set_param(const std::string& name,
                                            const std::string& value)
{
	if (name == PASSWORD) {
		password = value;
		return 0;
	}

        LOG_DBG("Unknown policy-set-specific parameters to set (%s, %s)",
                        name.c_str(), value.c_str());
        return -1;
}

//Class ISecurity Manager
ISecurityManager::~ISecurityManager()
{
	std::list<IAuthPolicySet*> entries = auth_policy_sets.getEntries();
	std::list<IAuthPolicySet*>::iterator it;
	IAuthPolicySet * currentPs = 0;
	for (it = entries.begin(); it != entries.end(); ++it) {
		currentPs = (*it);
		app->psDestroy(rina::ApplicationEntity::SECURITY_MANAGER_AE_NAME,
			       currentPs->type,
			       currentPs);
	}

	std::list<ISecurityContext*> contexts = security_contexts.getEntries();
	std::list<ISecurityContext*>::iterator it2;
	ISecurityContext * context = 0;
	for (it2 = contexts.begin(); it2 != contexts.end(); ++it2) {
		context = (*it2);
		if (context) {
			delete context;
		}
	}
}

int ISecurityManager::add_auth_policy_set(const std::string& auth_type)
{
        IAuthPolicySet *candidate = NULL;

        if (!app) {
                LOG_ERR("bug: NULL app reference");
                return -1;
        }

        if (get_auth_policy_set(auth_type)) {
                LOG_INFO("authentication policy set %s already included in the security-manager",
                         auth_type.c_str());
                return 0;
        }

        candidate = (IAuthPolicySet *) app->psCreate(ApplicationEntity::SECURITY_MANAGER_AE_NAME,
        					     auth_type,
        					     this);
        if (!candidate) {
                LOG_ERR("failed to allocate instance of policy set %s", auth_type.c_str());
                return -1;
        }

        // Install the new one.
        auth_policy_sets.put(auth_type, candidate);
        LOG_INFO("Authentication policy-set %s added to the security-manager",
                        auth_type.c_str());

        return 0;
}

IAuthPolicySet * ISecurityManager::get_auth_policy_set(const std::string& auth_type)
{
	return auth_policy_sets.find(auth_type);
}

ISecurityContext * ISecurityManager::get_security_context(int context_id)
{
	return security_contexts.find(context_id);
}

void ISecurityManager::eventHappened(InternalEvent * event)
{
	NMinusOneFlowDeallocatedEvent * n_event = 0;
	ISecurityContext * context = 0;

	if (event->type == InternalEvent::APP_N_MINUS_1_FLOW_DEALLOCATED) {
		n_event = dynamic_cast<NMinusOneFlowDeallocatedEvent *>(event);
		context = security_contexts.erase(n_event->cdap_session_descriptor_.port_id_);
		if (context) {
			delete context;
		}
	}
}

}


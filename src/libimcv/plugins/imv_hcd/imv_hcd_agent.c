/*
 * Copyright (C) 2015 Andreas Steffen
 * HSR Hochschule fuer Technik Rapperswil
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#define _GNU_SOURCE
#include <stdio.h>

#include "imv_hcd_agent.h"
#include "imv_hcd_state.h"

#include <imcv.h>
#include <imv/imv_agent.h>
#include <imv/imv_msg.h>
#include <generic/generic_attr_bool.h>
#include <generic/generic_attr_chunk.h>
#include <generic/generic_attr_string.h>
#include <ietf/ietf_attr.h>
#include <ietf/ietf_attr_attr_request.h>
#include <pwg/pwg_attr.h>
#include <pwg/pwg_attr_vendor_smi_code.h>
#include "tcg/seg/tcg_seg_attr_max_size.h"
#include "tcg/seg/tcg_seg_attr_seg_env.h"

#include <tncif_names.h>
#include <tncif_pa_subtypes.h>

#include <pen/pen.h>
#include <utils/debug.h>

#define HCD_MAX_ATTR_SIZE	10000000

typedef struct private_imv_hcd_agent_t private_imv_hcd_agent_t;
typedef enum imv_hcd_attr_t imv_hcd_attr_t;

/* Subscribed PA-TNC message subtypes */
static pen_type_t msg_types[] = {
	{ PEN_PWG, PA_SUBTYPE_PWG_HCD }
};

/**
 * Flag set when corresponding attribute has been received
 */
enum imv_hcd_attr_t {
	IMV_HCD_ATTR_NONE =                          0,
	IMV_HCD_ATTR_NATURAL_LANG =              (1<<0),
	IMV_HCD_ATTR_DEFAULT_PWD_ENABLED =       (1<<1),
	IMV_HCD_ATTR_FIREWALL_SETTING =          (1<<2),
	IMV_HCD_ATTR_FIRMWARE_NAME =             (1<<3),
	IMV_HCD_ATTR_FORWARDING_ENABLED =        (1<<4),
	IMV_HCD_ATTR_MACHINE_TYPE_MODEL =        (1<<5),
	IMV_HCD_ATTR_PSTN_FAX_ENABLED =          (1<<6),
	IMV_HCD_ATTR_RESIDENT_APP_NAME =         (1<<7),
	IMV_HCD_ATTR_TIME_SOURCE =               (1<<8),
	IMV_HCD_ATTR_USER_APP_ENABLED =          (1<<9),
	IMV_HCD_ATTR_USER_APP_PERSIST_ENABLED =  (1<<10),
	IMV_HCD_ATTR_USER_APP_NAME =             (1<<11),
	IMV_HCD_ATTR_VENDOR_NAME =               (1<<12),
	IMV_HCD_ATTR_VENDOR_SMI_CODE =           (1<<13),
	IMV_HCD_ATTR_MUST =                      (1<<14)-1
};

static imv_hcd_attr_t attr_type_to_flag(pwg_attr_t attr_type)
{
	switch (attr_type)
	{
		case PWG_HCD_ATTRS_NATURAL_LANG:
			return IMV_HCD_ATTR_NATURAL_LANG;
		case PWG_HCD_DEFAULT_PWD_ENABLED:
			return IMV_HCD_ATTR_DEFAULT_PWD_ENABLED;
		case PWG_HCD_FIREWALL_SETTING:
			return IMV_HCD_ATTR_FIREWALL_SETTING;
		case PWG_HCD_FIRMWARE_NAME:
			return IMV_HCD_ATTR_FIRMWARE_NAME;
		case PWG_HCD_FORWARDING_ENABLED:
			return IMV_HCD_ATTR_FORWARDING_ENABLED;
		case PWG_HCD_MACHINE_TYPE_MODEL:
			return IMV_HCD_ATTR_MACHINE_TYPE_MODEL;
		case PWG_HCD_PSTN_FAX_ENABLED:
			return IMV_HCD_ATTR_PSTN_FAX_ENABLED;
		case PWG_HCD_RESIDENT_APP_NAME:
			return IMV_HCD_ATTR_RESIDENT_APP_NAME;
		case PWG_HCD_TIME_SOURCE:
			return IMV_HCD_ATTR_TIME_SOURCE;
		case PWG_HCD_USER_APP_ENABLED:
			return IMV_HCD_ATTR_USER_APP_ENABLED;
		case PWG_HCD_USER_APP_PERSIST_ENABLED:
			return IMV_HCD_ATTR_USER_APP_PERSIST_ENABLED;
		case PWG_HCD_USER_APP_NAME:
			return IMV_HCD_ATTR_USER_APP_NAME;
		case PWG_HCD_VENDOR_NAME:
			return IMV_HCD_ATTR_VENDOR_NAME;
		case PWG_HCD_VENDOR_SMI_CODE:
			return IMV_HCD_ATTR_VENDOR_SMI_CODE;
		default:
			return IMV_HCD_ATTR_NONE;
	}
}

/**
 * Private data of an imv_hcd_agent_t object.
 */
struct private_imv_hcd_agent_t {

	/**
	 * Public members of imv_hcd_agent_t
	 */
	imv_agent_if_t public;

	/**
	 * IMV agent responsible for generic functions
	 */
	imv_agent_t *agent;

};

METHOD(imv_agent_if_t, bind_functions, TNC_Result,
	private_imv_hcd_agent_t *this, TNC_TNCS_BindFunctionPointer bind_function)
{
	return this->agent->bind_functions(this->agent, bind_function);
}

METHOD(imv_agent_if_t, notify_connection_change, TNC_Result,
	private_imv_hcd_agent_t *this, TNC_ConnectionID id,
	TNC_ConnectionState new_state)
{
	TNC_IMV_Action_Recommendation rec;
	imv_state_t *state;
	imv_session_t *session;

	switch (new_state)
	{
		case TNC_CONNECTION_STATE_CREATE:
			state = imv_hcd_state_create(id);
			return this->agent->create_state(this->agent, state);
		case TNC_CONNECTION_STATE_DELETE:
			return this->agent->delete_state(this->agent, id);
		case TNC_CONNECTION_STATE_ACCESS_ALLOWED:
		case TNC_CONNECTION_STATE_ACCESS_ISOLATED:
		case TNC_CONNECTION_STATE_ACCESS_NONE:
			if (this->agent->get_state(this->agent, id, &state) && imcv_db)
			{
				session = state->get_session(state);

				if (session->get_policy_started(session))
				{
					switch (new_state)
					{
						case TNC_CONNECTION_STATE_ACCESS_ALLOWED:
							rec = TNC_IMV_ACTION_RECOMMENDATION_ALLOW;
							break;
						case TNC_CONNECTION_STATE_ACCESS_ISOLATED:
							rec = TNC_IMV_ACTION_RECOMMENDATION_ISOLATE;
							break;
						case TNC_CONNECTION_STATE_ACCESS_NONE:
						default:
							rec = TNC_IMV_ACTION_RECOMMENDATION_NO_ACCESS;
					}
					imcv_db->add_recommendation(imcv_db, session, rec);
					if (!imcv_db->policy_script(imcv_db, session, FALSE))
					{
						DBG1(DBG_IMV, "error in policy script stop");
					}
				}
			}
			/* fall through to default state */
		default:
			return this->agent->change_state(this->agent, id, new_state, NULL);
	}
}

/**
 * Process a received message
 */
static TNC_Result receive_msg(private_imv_hcd_agent_t *this, imv_state_t *state,
							  imv_msg_t *in_msg)
{
	imv_msg_t *out_msg;
	imv_hcd_state_t *hcd_state;
	pa_tnc_attr_t *attr;
	pen_type_t type;
	TNC_Result result;
	bool fatal_error = FALSE, assessment = FALSE;
	enumerator_t *enumerator;

	hcd_state = (imv_hcd_state_t*)state;

	/* generate an outgoing PA-TNC message - we might need it */
	out_msg = imv_msg_create_as_reply(in_msg);

	/* parse received PA-TNC message and handle local and remote errors */
	result = in_msg->receive(in_msg,out_msg, &fatal_error);
	if (result != TNC_RESULT_SUCCESS)
	{
		out_msg->destroy(out_msg);
		return result;
	}

	/* analyze PA-TNC attributes */
	enumerator = in_msg->create_attribute_enumerator(in_msg);
	while (enumerator->enumerate(enumerator, &attr))
	{
		type = attr->get_type(attr);

		if (type.vendor_id == PEN_PWG)
		{
			state->set_action_flags(state, attr_type_to_flag(type.type));

			switch (type.type)
			{
				case PWG_HCD_ATTRS_NATURAL_LANG:
				case PWG_HCD_MACHINE_TYPE_MODEL:
				case PWG_HCD_VENDOR_NAME:
				case PWG_HCD_TIME_SOURCE:
				case PWG_HCD_FIRMWARE_NAME:
				case PWG_HCD_FIRMWARE_PATCHES:
				case PWG_HCD_FIRMWARE_STRING_VERSION:
				case PWG_HCD_RESIDENT_APP_NAME:
				case PWG_HCD_RESIDENT_APP_PATCHES:
				case PWG_HCD_RESIDENT_APP_STRING_VERSION:
				case PWG_HCD_USER_APP_NAME:
				case PWG_HCD_USER_APP_PATCHES:
				case PWG_HCD_USER_APP_STRING_VERSION:
				{
					chunk_t value;

					value = attr->get_value(attr);
					DBG2(DBG_IMV, "%N: %.*s", pwg_attr_names, type.type,
								   value.len, value.ptr);
					break;
				}
				case PWG_HCD_FIRMWARE_VERSION:
				case PWG_HCD_RESIDENT_APP_VERSION:
				case PWG_HCD_USER_APP_VERSION:
				{
					chunk_t value;

					value = attr->get_value(attr);
					DBG2(DBG_IMV, "%N: %#B", pwg_attr_names, type.type, &value);
					break;
				}
				case PWG_HCD_CERTIFICATION_STATE:
				case PWG_HCD_CONFIGURATION_STATE:
				{
					chunk_t value;

					value = attr->get_value(attr);
					DBG2(DBG_IMV, "%N: %B", pwg_attr_names, type.type, &value);
					break;
				}
				case PWG_HCD_DEFAULT_PWD_ENABLED:
				case PWG_HCD_FORWARDING_ENABLED:
				case PWG_HCD_PSTN_FAX_ENABLED:
				case PWG_HCD_USER_APP_ENABLED:
				case PWG_HCD_USER_APP_PERSIST_ENABLED:
				{
					generic_attr_bool_t *attr_cast;
					bool status;

					attr_cast = (generic_attr_bool_t*)attr;
					status = attr_cast->get_status(attr_cast);
					DBG2(DBG_IMV, "%N: %s", pwg_attr_names, type.type,
								   status ? "yes" : "no");

					if (type.type == PWG_HCD_USER_APP_ENABLED && !status)
					{
						/* do not request user applications */
						state->set_action_flags(state,
												IMV_HCD_ATTR_USER_APP_NAME);
					}		
					break;
				}
				case PWG_HCD_VENDOR_SMI_CODE:
				{
					pwg_attr_vendor_smi_code_t *attr_cast;
					uint32_t smi_code;

					attr_cast = (pwg_attr_vendor_smi_code_t*)attr;
					smi_code = attr_cast->get_vendor_smi_code(attr_cast);
					DBG2(DBG_IMV, "%N: 0x%06x (%u)", pwg_attr_names, type.type,
								  smi_code, smi_code);
					break;
				}
				default:
					break;
			}
		}
	}
	enumerator->destroy(enumerator);

	if (fatal_error)
	{
		state->set_recommendation(state,
								TNC_IMV_ACTION_RECOMMENDATION_NO_RECOMMENDATION,
								TNC_IMV_EVALUATION_RESULT_ERROR);
		assessment = TRUE;
	}

	if (assessment)
	{
		hcd_state->set_handshake_state(hcd_state, IMV_HCD_STATE_END);
		result = out_msg->send_assessment(out_msg);
		if (result == TNC_RESULT_SUCCESS)
		{
			result = this->agent->provide_recommendation(this->agent, state);
		}
	}
	else
	{
		/* send PA-TNC message with the EXCL flag set */
		result = out_msg->send(out_msg, TRUE);
	}
	out_msg->destroy(out_msg);

	return result;
}

METHOD(imv_agent_if_t, receive_message, TNC_Result,
	private_imv_hcd_agent_t *this, TNC_ConnectionID id,
	TNC_MessageType msg_type, chunk_t msg)
{
	imv_state_t *state;
	imv_msg_t *in_msg;
	TNC_Result result;

	if (!this->agent->get_state(this->agent, id, &state))
	{
		return TNC_RESULT_FATAL;
	}
	in_msg = imv_msg_create_from_data(this->agent, state, id, msg_type, msg);
	result = receive_msg(this, state, in_msg);
	in_msg->destroy(in_msg);

	return result;
}

METHOD(imv_agent_if_t, receive_message_long, TNC_Result,
	private_imv_hcd_agent_t *this, TNC_ConnectionID id,
	TNC_UInt32 src_imc_id, TNC_UInt32 dst_imv_id,
	TNC_VendorID msg_vid, TNC_MessageSubtype msg_subtype, chunk_t msg)
{
	imv_state_t *state;
	imv_msg_t *in_msg;
	TNC_Result result;

	if (!this->agent->get_state(this->agent, id, &state))
	{
		return TNC_RESULT_FATAL;
	}
	in_msg = imv_msg_create_from_long_data(this->agent, state, id,
					src_imc_id, dst_imv_id, msg_vid, msg_subtype, msg);
	result = receive_msg(this, state, in_msg);
	in_msg->destroy(in_msg);

	return result;

}

/**
 * Build an IETF Attribute Request attribute for missing attributes
 */
static pa_tnc_attr_t* build_attr_request(uint32_t received)
{
	pa_tnc_attr_t *attr;
	ietf_attr_attr_request_t *attr_cast;

	attr = ietf_attr_attr_request_create(PEN_RESERVED, 0);
	attr_cast = (ietf_attr_attr_request_t*)attr;

	if (!(received & IMV_HCD_ATTR_NATURAL_LANG))
	{
		attr_cast->add(attr_cast, PEN_PWG, PWG_HCD_ATTRS_NATURAL_LANG);
	}
	if (!(received & IMV_HCD_ATTR_DEFAULT_PWD_ENABLED))
	{
		attr_cast->add(attr_cast, PEN_PWG, PWG_HCD_DEFAULT_PWD_ENABLED);
	}
	if (!(received & IMV_HCD_ATTR_FIREWALL_SETTING))
	{
		attr_cast->add(attr_cast, PEN_PWG, PWG_HCD_FIREWALL_SETTING);
	}
	if (!(received & IMV_HCD_ATTR_FIRMWARE_NAME))
	{
		attr_cast->add(attr_cast, PEN_PWG, PWG_HCD_FIRMWARE_NAME);
	}
	if (!(received & IMV_HCD_ATTR_FORWARDING_ENABLED))
	{
		attr_cast->add(attr_cast, PEN_PWG, PWG_HCD_FORWARDING_ENABLED);
	}
	if (!(received & IMV_HCD_ATTR_MACHINE_TYPE_MODEL))
	{
		attr_cast->add(attr_cast, PEN_PWG, PWG_HCD_MACHINE_TYPE_MODEL);
	}
	if (!(received & IMV_HCD_ATTR_PSTN_FAX_ENABLED))
	{
		attr_cast->add(attr_cast, PEN_PWG, PWG_HCD_PSTN_FAX_ENABLED);
	}
	if (!(received & IMV_HCD_ATTR_RESIDENT_APP_NAME))
	{
		attr_cast->add(attr_cast, PEN_PWG, PWG_HCD_RESIDENT_APP_NAME);
	}
	if (!(received & IMV_HCD_ATTR_TIME_SOURCE))
	{
		attr_cast->add(attr_cast, PEN_PWG, PWG_HCD_TIME_SOURCE);
	}
	if (!(received & IMV_HCD_ATTR_USER_APP_ENABLED))
	{
		attr_cast->add(attr_cast, PEN_PWG, PWG_HCD_USER_APP_ENABLED);
	}
	if (!(received & IMV_HCD_ATTR_USER_APP_PERSIST_ENABLED))
	{
		attr_cast->add(attr_cast, PEN_PWG, PWG_HCD_USER_APP_PERSIST_ENABLED);
	}
	if (!(received & IMV_HCD_ATTR_USER_APP_NAME))
	{
		attr_cast->add(attr_cast, PEN_PWG, PWG_HCD_USER_APP_NAME);
	}
	if (!(received & IMV_HCD_ATTR_VENDOR_NAME))
	{
		attr_cast->add(attr_cast, PEN_PWG, PWG_HCD_VENDOR_NAME);
	}
	if (!(received & IMV_HCD_ATTR_VENDOR_SMI_CODE))
	{
		attr_cast->add(attr_cast, PEN_PWG, PWG_HCD_VENDOR_SMI_CODE);
	}
	attr_cast->add(attr_cast, PEN_PWG, PWG_HCD_CERTIFICATION_STATE);
	attr_cast->add(attr_cast, PEN_PWG, PWG_HCD_CONFIGURATION_STATE);

	return attr;
}

METHOD(imv_agent_if_t, batch_ending, TNC_Result,
	private_imv_hcd_agent_t *this, TNC_ConnectionID id)
{
	imv_msg_t *out_msg;
	imv_state_t *state;
	imv_hcd_state_t *hcd_state;
	imv_hcd_handshake_state_t handshake_state;
	pa_tnc_attr_t *attr;
	TNC_IMVID imv_id;
	TNC_Result result = TNC_RESULT_SUCCESS;
	uint32_t received;

	if (!this->agent->get_state(this->agent, id, &state))
	{
		return TNC_RESULT_FATAL;
	}
	hcd_state = (imv_hcd_state_t*)state;
	handshake_state = hcd_state->get_handshake_state(hcd_state);
	received = state->get_action_flags(state);
	imv_id = this->agent->get_id(this->agent);

	if (handshake_state == IMV_HCD_STATE_END)
	{
		return TNC_RESULT_SUCCESS;
	}

	/* create an empty out message - we might need it */
	out_msg = imv_msg_create(this->agent, state, id, imv_id, TNC_IMCID_ANY,
							 msg_types[0]);

	if (handshake_state == IMV_HCD_STATE_INIT)
	{
		size_t max_attr_size = HCD_MAX_ATTR_SIZE;
		size_t max_seg_size;
		seg_contract_t *contract;
		seg_contract_manager_t *contracts;
		char buf[BUF_LEN];

		/* Determine maximum PA-TNC attribute segment size */
		max_seg_size = state->get_max_msg_len(state)
								- PA_TNC_HEADER_SIZE 
								- PA_TNC_ATTR_HEADER_SIZE
								- TCG_SEG_ATTR_SEG_ENV_HEADER
								- PA_TNC_ATTR_HEADER_SIZE
								- TCG_SEG_ATTR_MAX_SIZE_SIZE;

		/* Announce support of PA-TNC segmentation to IMC */
		contract = seg_contract_create(msg_types[0], max_attr_size,
										max_seg_size, TRUE, imv_id, FALSE);
		contract->get_info_string(contract, buf, BUF_LEN, TRUE);
		DBG2(DBG_IMV, "%s", buf);
		contracts = state->get_contracts(state);
		contracts->add_contract(contracts, contract);
		attr = tcg_seg_attr_max_size_create(max_attr_size, max_seg_size, TRUE);
		out_msg->add_attribute(out_msg, attr);

		if ((received & IMV_HCD_ATTR_MUST) != IMV_HCD_ATTR_MUST)
		{
			/* create attribute request for missing mandatory attributes */
			out_msg->add_attribute(out_msg, build_attr_request(received));
		}
		hcd_state->set_handshake_state(hcd_state, IMV_HCD_STATE_ATTR_REQ);
	}

	/* send non-empty PA-TNC message with excl flag not set */
	if (out_msg->get_attribute_count(out_msg))
	{
		result = out_msg->send(out_msg, FALSE);
	}
	out_msg->destroy(out_msg);

	return result;
}

METHOD(imv_agent_if_t, solicit_recommendation, TNC_Result,
	private_imv_hcd_agent_t *this, TNC_ConnectionID id)
{
	imv_state_t *state;

	if (!this->agent->get_state(this->agent, id, &state))
	{
		return TNC_RESULT_FATAL;
	}
	return this->agent->provide_recommendation(this->agent, state);
}

METHOD(imv_agent_if_t, destroy, void,
	private_imv_hcd_agent_t *this)
{
	DESTROY_IF(this->agent);
	free(this);
}

/**
 * Described in header.
 */
imv_agent_if_t *imv_hcd_agent_create(const char *name, TNC_IMVID id,
									TNC_Version *actual_version)
{
	private_imv_hcd_agent_t *this;
	imv_agent_t *agent;

	agent = imv_agent_create(name, msg_types, countof(msg_types), id,
							 actual_version);
	if (!agent)
	{
		return NULL;
	}

	INIT(this,
		.public = {
			.bind_functions = _bind_functions,
			.notify_connection_change = _notify_connection_change,
			.receive_message = _receive_message,
			.receive_message_long = _receive_message_long,
			.batch_ending = _batch_ending,
			.solicit_recommendation = _solicit_recommendation,
			.destroy = _destroy,
		},
		.agent = agent,
	);

	return &this->public;
}


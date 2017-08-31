/*
 * $QNXLicenseC:
 * Copyright 2007, QNX Software Systems. All Rights Reserved.
 * 
 * You must obtain a written license from and pay applicable license fees to QNX 
 * Software Systems before you may reproduce, modify or distribute this software, 
 * or any work that includes all or part of this software.   Free development 
 * licenses are available for evaluation and non-commercial purposes.  For more 
 * information visit http://licensing.qnx.com or email licensing@qnx.com.
 *  
 * This file may contain contributions from others.  Please review this entire 
 * file for other proprietary rights or license notices, as well as the QNX 
 * Development Suite License Guide at http://licensing.qnx.com/license-guide/ 
 * for other information.
 * $
 */

#include "externs.h"
#include "apm.h"

struct kerargs_mempart_associate
{
	bool  associate;
	bool  disassociate;
	mempart_dcmd_flags_t  associate_flags;
	mempart_id_t  mempart_id;
	PROCESS *  prp;
	int  err;
};

static void
kerext_mempart_associate(void *data)
{
	struct kerargs_mempart_associate  *kap = data;

	/* always process disassociations first */
	if (kap->disassociate) {
		kap->err = MEMPART_DISASSOCIATE(kap->prp, kap->mempart_id);
	}
	if ((kap->err == EOK) && (kap->associate)) {
		kap->err = MEMPART_ASSOCIATE(kap->prp, kap->mempart_id, kap->associate_flags);
	}
}

/*
 * ProcessAssociate
 * 
 * Associate process <prp> with partition <mpid>. The process must not
 * be associated with a partition of the same memory class as <mpid>
 * otherwise an error will be returned
*/
int ProcessAssociate(PROCESS *prp, mempart_id_t mpid, mempart_dcmd_flags_t flags)
{
	struct kerargs_mempart_associate  data;

	data.prp = prp;
	data.mempart_id = mpid;
	data.err = EOK;
	data.associate = bool_t_TRUE;
	data.disassociate = bool_t_FALSE;
	data.associate_flags = flags;
	
	__Ring0(kerext_mempart_associate, &data);

	return data.err;
}

/*
 * ProcessDisassociate
 * 
 * Disassociate process <prp> from partition <mpid>
*/
int ProcessDisassociate(PROCESS *prp, mempart_id_t mpid)
{
	struct kerargs_mempart_associate  data;

	data.prp = prp;
	data.mempart_id = mpid;
	data.err = EOK;
	data.associate = bool_t_FALSE;
	data.disassociate = bool_t_TRUE;
	data.associate_flags = mempart_flags_NONE;
	
	__Ring0(kerext_mempart_associate, &data);

	return data.err;
}

/*
 * ProcessReassociate
 * 
 * change process association to <mempart_id>. The process <prp> must already
 * be associated with a partition of the same memory class as <mempart_id>
 * otherwise an error will be returned
*/
int ProcessReassociate(PROCESS *prp, mempart_id_t mpid, mempart_dcmd_flags_t flags)
{
	struct kerargs_mempart_associate  data;

	data.prp = prp;
	data.mempart_id = mpid;
	data.err = EOK;
	data.associate = bool_t_TRUE;
	data.disassociate = bool_t_TRUE;
	data.associate_flags = flags;
	
	__Ring0(kerext_mempart_associate, &data);

	return data.err;
}

/*
 * ValidateMpartId
 * 
 * Validate a partition identifier sent from user space
*/
int ValidateMpartId(mempart_id_t mpid)
{
	THREAD *act = actives[RUNCPU];
	mempart_t *mpart = MEMPART_ID_TO_T(mpid);

	KASSERT(MEMPART_INSTALLED());
	
	RD_VERIFY_PTR(act, mpart, sizeof(*mpart));
	RD_PROBE_INT(act, mpart, sizeof(mpart->signature));

	if (mpart == NULL)
		return EBADF;
	if (mpart->signature == MEMPART_SIGNATURE)
		return EOK;
	else
		return EINVAL;
}


__SRCVERSION("kerext_mempart.c $Rev: 153001 $");

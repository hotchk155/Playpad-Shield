#include "Playpad.h"
#include "SynchFIFO.h"

void fifo_init(SynchFIFO *p, int size)
{
	p->size = size;
	p->head = 0;
	p->tail = 0;
	p->pdata = malloc(size * sizeof(FIFO_ENTRY));
	vos_init_mutex(&p->mAccess,0);		
}

#define FIFO_INC(p, v) {if((++v) >= p->size) v=0;}

void fifo_put(SynchFIFO *p, FIFO_ENTRY v)
{
	int next;
	vos_lock_mutex(&p->mAccess);	
	next = p->head;
	FIFO_INC(p, next);
	if(next != p->tail)
	{		
		p->pdata[p->head] = v;
		p->head = next;
	}
	vos_unlock_mutex(&p->mAccess);	
}

FIFO_ENTRY fifo_get(SynchFIFO *p)
{
	FIFO_ENTRY v = 0;
	vos_lock_mutex(&p->mAccess);	
	if(p->tail != p->head)
	{
		v = p->pdata[p->tail];
		FIFO_INC(p, p->tail);
	}
	vos_unlock_mutex(&p->mAccess);	
	return v;	
}
	
int fifo_full(SynchFIFO *p)
{
	int next, full;
	vos_lock_mutex(&p->mAccess);	
	next = p->head;
	FIFO_INC(p, next);
	full = (next == p->tail);
	vos_unlock_mutex(&p->mAccess);	
	return full;
}
	
int fifo_avail(SynchFIFO *p)
{
	int avail;
	vos_lock_mutex(&p->mAccess);	
	avail = (p->head != p->tail);
	vos_unlock_mutex(&p->mAccess);	
	return avail;
}


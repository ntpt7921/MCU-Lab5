/*
 * fsm.h
 *
 *  Created on: Nov 1, 2022
 *      Author: ntpt
 */

#include "Custom/fsm.h"

FsmState_t Custom_Fsm_GetNextState(Fsm_t *fsm)
{
    return fsm->get_next_state(fsm->current_state);
}

void Custom_Fsm_SetNextState(Fsm_t *fsm)
{
    FsmState_t next_state = Custom_Fsm_GetNextState(fsm);
    fsm->current_state = next_state;
}

void Custom_Fsm_DoInState(Fsm_t *fsm)
{
    fsm->do_in_state();
}

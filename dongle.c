/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   dongle.c                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mide-fre <mide-fre@student.42porto.com>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/22 00:16:53 by mide-fre          #+#    #+#             */
/*   Updated: 2026/08/24 19:05:13 by mide-fre         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "codexion.h"

void	request_init(t_coder *c, t_request *req)
{
	req->coder_id = c->id;
	req->seq = next_seq(c->sim);
	req->deadline = coder_deadline(c);
	req->owner = c;
	req->idx = -1;
}

int	wait_pair(t_coder *c, t_request *r1, t_request *r2)
{
	struct timespec	ts;
	long			now;

	now = now_ms();
	while (!sim_stopped(c->sim) && !can_take_pair(c, r1, r2, now))
	{
		ms_to_timespec(pair_wake(c, now), &ts);
		pthread_cond_timedwait(&c->sim->arb_cond, &c->sim->arb_lock, &ts);
		now = now_ms();
	}
	return (!sim_stopped(c->sim));
}

int	acquire_pair(t_coder *c, t_request *r1, t_request *r2)
{
	int	ok;

	pthread_mutex_lock(&c->sim->arb_lock);
	heap_push(&c->first->queue, r1);
	heap_push(&c->second->queue, r2);
	pthread_cond_broadcast(&c->sim->arb_cond);
	ok = wait_pair(c, r1, r2);
	heap_remove(&c->first->queue, r1);
	heap_remove(&c->second->queue, r2);
	if (ok)
	{
		c->first->available = 0;
		c->second->available = 0;
	}
	pthread_mutex_unlock(&c->sim->arb_lock);
	return (ok);
}

int	dongle_acquire(t_coder *c)
{
	t_request	r1;
	t_request	r2;

	request_init(c, &r1);
	r2 = r1;
	if (!acquire_pair(c, &r1, &r2))
		return (0);
	log_state(c, "has taken a dongle");
	log_state(c, "has taken a dongle");
	return (1);
}

void	dongle_release(t_dongle *d, t_sim *sim)
{
	pthread_mutex_lock(&sim->arb_lock);
	d->available = 1;
	d->available_at = now_ms() + sim->cooldown;
	pthread_cond_broadcast(&sim->arb_cond);
	pthread_mutex_unlock(&sim->arb_lock);
}

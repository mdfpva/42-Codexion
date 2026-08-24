/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   dongle.c                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mide-fre <mide-fre@student.42porto.com>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/22 00:16:53 by mide-fre          #+#    #+#             */
/*   Updated: 2026/08/24 17:23:39 by mide-fre         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "codexion.h"

void	request_init(t_coder *c, t_request *req)
{
	req->coder_id = c->id;
	req->seq = next_seq(c->sim);
	req->deadline = coder_deadline(c);
	req->idx = -1;
}

int	wait_pair(t_coder *c, t_request *r1, t_request *r2)
{
	struct timespec	ts;
	long			now;

	now = now_ms();
	while (!sim_stopped(c->sim) && !pair_ready(c, r1, r2, now))
	{
		ms_to_timespec(pair_wake(c, now), &ts);
		pthread_mutex_unlock(&c->second->lock);
		pthread_cond_timedwait(&c->first->cond, &c->first->lock, &ts);
		pthread_mutex_lock(&c->second->lock);
		now = now_ms();
	}
	return (!sim_stopped(c->sim));
}

int	acquire_pair(t_coder *c, t_request *r1, t_request *r2)
{
	pthread_mutex_lock(&c->first->lock);
	pthread_mutex_lock(&c->second->lock);
	heap_push(&c->first->queue, r1);
	heap_push(&c->second->queue, r2);
	pthread_cond_broadcast(&c->first->cond);
	pthread_cond_broadcast(&c->second->cond);
	if (!wait_pair(c, r1, r2))
	{
		heap_remove(&c->first->queue, r1);
		heap_remove(&c->second->queue, r2);
		pthread_mutex_unlock(&c->second->lock);
		pthread_mutex_unlock(&c->first->lock);
		return (0);
	}
	heap_pop(&c->first->queue);
	heap_pop(&c->second->queue);
	c->first->available = 0;
	c->second->available = 0;
	pthread_mutex_unlock(&c->second->lock);
	pthread_mutex_unlock(&c->first->lock);
	return (1);
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
	pthread_mutex_lock(&d->lock);
	d->available = 1;
	d->available_at = now_ms() + sim->cooldown;
	pthread_cond_broadcast(&d->cond);
	pthread_mutex_unlock(&d->lock);
}

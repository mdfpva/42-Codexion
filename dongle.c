/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   dongle.c                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mide-fre <mide-fre@student.42porto.com>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/22 00:16:53 by mide-fre          #+#    #+#             */
/*   Updated: 2026/08/24 15:26:36 by mide-fre         ###   ########.fr       */
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

int	wait_turn(t_coder *c, t_dongle *d, t_request *req)
{
	struct timespec	ts;
	long			now;

	now = now_ms();
	while (!sim_stopped(c->sim) && !can_take(d, req, now))
	{
		ms_to_timespec(wake_time(d, now), &ts);
		pthread_cond_timedwait(&d->cond, &d->lock, &ts);
		now = now_ms();
	}
	return (!sim_stopped(c->sim));
}

int	dongle_acquire(t_coder *c, t_dongle *d)
{
	t_request	req;

	request_init(c, &req);
	pthread_mutex_lock(&d->lock);
	heap_push(&d->queue, &req);
	pthread_cond_broadcast(&d->cond);
	if (!wait_turn(c, d, &req))
	{
		heap_remove(&d->queue, &req);
		pthread_mutex_unlock(&d->lock);
		return (0);
	}
	heap_pop(&d->queue);
	d->available = 0;
	pthread_mutex_unlock(&d->lock);
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

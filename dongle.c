/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   dongle.c                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mide-fre <mide-fre@student.42porto.com>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/22 00:16:53 by mide-fre          #+#    #+#             */
/*   Updated: 2026/08/22 00:20:55 by mide-fre         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "codexion.h"

int	dongle_acquire(t_coder *c, t_dongle *d)
{
	t_request		req;
	struct timespec	ts;
	long			now;

	req.coder_id = c->id;
	req.seq = next_seq(c->sim);
	req.deadline = coder_deadline(c);
	req.idx = -1;
	pthread_mutex_lock(&d->lock);
	heap_push(&d->queue, &req);
	pthread_cond_broadcast(&d->cond);
	now = now_ms();
	while (!sim_stopped(c->sim) && !can_take(d, &req, now))
	{
		ms_to_timespec(wake_time(d, now), &ts);
		pthread_cond_timedwait(&d->cond, &d->lock, &ts);
		now = now_ms();
	}
	if (sim_stopped(c->sim))
		return (heap_remove(&d->queue, &req),
			pthread_mutex_unlock(&d->lock), 0);
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

void	assign_dongles(t_sim *sim, int i)
{
	t_dongle	*left;
	t_dongle	*right;

	left = &sim->dongles[i];
	right = &sim->dongles[(i + 1) % sim->n_coders];
	if (left->id < right->id)
	{
		sim->coders[i].first = left;
		sim->coders[i].second = right;
	}
	else
	{
		sim->coders[i].first = right;
		sim->coders[i].second = left;
	}
}

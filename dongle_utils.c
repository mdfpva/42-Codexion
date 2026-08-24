/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   dongle_utils.c                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mide-fre <mide-fre@student.42porto.com>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/22 00:16:13 by mide-fre          #+#    #+#             */
/*   Updated: 2026/08/24 15:31:48 by mide-fre         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "codexion.h"

void	ms_to_timespec(long target_ms, struct timespec *ts)
{
	ts->tv_sec = target_ms / 1000;
	ts->tv_nsec = (target_ms % 1000) * 1000000;
}

long	wake_time(t_dongle *d, long now)
{
	if (d->available && d->available_at > now)
		return (d->available_at);
	return (now + 1);
}

int	can_take(t_dongle *d, t_request *req, long now)
{
	return (d->available && now >= d->available_at
		&& heap_peek(&d->queue) == req);
}

long	pair_wake(t_coder *c, long now)
{
	long	w1;
	long	w2;

	w1 = wake_time(c->first, now);
	w2 = wake_time(c->second, now);
	if (w1 < w2)
		return (w1);
	return (w2);
}

int	pair_ready(t_coder *c, t_request *r1, t_request *r2, long now)
{
	return (can_take(c->first, r1, now) && can_take(c->second, r2, now));
}

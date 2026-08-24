/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   dongle_utils.c                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mide-fre <mide-fre@student.42porto.com>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/22 00:16:13 by mide-fre          #+#    #+#             */
/*   Updated: 2026/08/24 19:04:29 by mide-fre         ###   ########.fr       */
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

int	dongle_free(t_dongle *d, long now)
{
	return (d->available && now >= d->available_at);
}

int	pair_free(t_coder *c, long now)
{
	return (dongle_free(c->first, now) && dongle_free(c->second, now));
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

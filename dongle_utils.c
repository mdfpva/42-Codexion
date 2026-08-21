/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   dongle_utils.c                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mide-fre <mide-fre@student.42porto.com>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/22 00:16:13 by mide-fre          #+#    #+#             */
/*   Updated: 2026/08/22 00:16:40 by mide-fre         ###   ########.fr       */
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

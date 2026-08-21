# **************************************************************************** #
#                                                                              #
#                                                         :::      ::::::::    #
#    Makefile                                           :+:      :+:    :+:    #
#                                                     +:+ +:+         +:+      #
#    By: mide-fre <mide-fre@student.42porto.com>    +#+  +:+       +#+         #
#                                                 +#+#+#+#+#+   +#+            #
#    Created: 2026/08/21 16:05:50 by mide-fre          #+#    #+#              #
#    Updated: 2026/08/22 00:29:20 by mide-fre         ###   ########.fr        #
#                                                                              #
# **************************************************************************** #

NAME = codexion

SRCS = codexion.c \
	heap.c heap_ops.c \
	time.c log.c \
	dongle_utils.c dongle.c

HDRS = codexion.h

OBJS = $(SRCS:.c=.o)

CC = cc
CFLAGS = -Wall -Wextra -Werror -pthread

all: $(NAME)

$(NAME): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re

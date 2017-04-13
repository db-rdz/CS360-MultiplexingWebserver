#!/bin/bash
exec echo $QUERY_STRING | /usr/bin/php-cgi

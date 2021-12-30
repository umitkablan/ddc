#!/bin/bash

source conf.sh

set -x

check_process() {
    local cnt=$(ps -ef | grep "$1" | wc -l)
    local res=0
    if [ $cnt -lt 2 ]; then
        res=1
        echo "$1 not found!"
    fi
    return $res
}

start_rabbitmq() {
    echo "starting RabbitMQ.."
    rabbitmq-server &
}

start_redis() {
    echo "starting Redis.."
    redis-server --daemonize yes
}

ddc_start() {
    LD_LIBRARY_PATH=../bin/lib ../bin/bin/qlistener  qlistener.conf &
    LD_LIBRARY_PATH=../bin/lib ../bin/bin/countersrv "$REDIS_URI"   &
}

setup_rabbitmq() {
    echo "setting up RabbitMQ.."

    # echo -n '[rabbitmq_management,rabbitmq_stomp,rabbitmq_amqp1_0,rabbitmq_mqtt,rabbitmq_stream]' > /etc/rabbitmq/enabled_plugins
    rabbitmq-plugins enable rabbitmq_management

    rabbitmqctl add_user "$AMQP_User" "$AMQP_Password"
    rabbitmqctl set_user_tags "$AMQP_User" management
    rabbitmqctl set_user_tags "$AMQP_User" administrator

    rabbitmqctl set_permissions -p /   "$AMQP_User" ".*" ".*" ".*"
    rabbitmqctl add_vhost          "$AMQP_VirtualHost"
    rabbitmqctl set_permissions -p "$AMQP_VirtualHost" "$AMQP_User" '.*' '.*' '.*'
    rabbitmqctl set_permissions -p "$AMQP_VirtualHost" "guest" ".*" ".*" ".*"

    rabbitmqadmin declare exchange --vhost="$AMQP_VirtualHost" name="$AMQP_Exchange" type=direct
    rabbitmqadmin declare queue    --vhost="$AMQP_VirtualHost" name="$AMQP_Queue"    durable=true
    rabbitmqadmin declare binding  --vhost="$AMQP_VirtualHost" source="$AMQP_Exchange" \
            destination_type=queue destination="$AMQP_Queue" routing_key="$AMQP_RoutingKey"
}

write_confs() {
    echo "writing DDC configuration files.."

    echo -n "$AMQP_User:$AMQP_Password" > userpass.txt

    echo "AMQPHost=$AMQP_ServerIP/$AMQP_VirtualHost"  > fake_dev_adapter.conf
    echo "AMQPUserPasswordFile=userpass.txt"         >> fake_dev_adapter.conf
    echo "AMQPRoutingKey=$AMQP_RoutingKey"           >> fake_dev_adapter.conf
    echo "AMQPExchange=$AMQP_Exchange"               >> fake_dev_adapter.conf

    echo "AMQPHost=$AMQP_ServerIP/$AMQP_VirtualHost"  > qlistener.conf
    echo "AMQPUserPasswordFile=userpass.txt"         >> qlistener.conf
    echo "AMQPQueue=$AMQP_Queue"                     >> qlistener.conf
    echo "RedisURI=$REDIS_URI"                       >> qlistener.conf
}

ddc_stop() {
    killall qlistener
    killall countersrv
}

stop_rabbitmq() {
    echo "stopping RabbitMQ.."
    killall rabbitmq-server
}

stop_redis() {
    echo "stopping Redis.."
    killall redis-server
}

case "$1" in
  "start")
    start_rabbitmq
    start_redis
    ;;

  "stop")
    stop_rabbitmq
    stop_redis
    ;;

  "check")
    check_process "rabbitmq-server"
    res="$?"
    check_process "redis-server"
    [[ $? == 0 || $res == 0 ]]
    ;;

  "setup")
    setup_rabbitmq
    write_confs
    ;;

  "ddc_start")
    ddc_start
    ;;

  "ddc_stop")
    ddc_stop
    ;;

  *)
    echo "Usage: $0 [start|stop]"
    ;;
esac


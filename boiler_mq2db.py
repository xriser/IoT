import paho.mqtt.client as mqtt #import the client1
import time
from influxdb import InfluxDBClient
from influxdb.client import InfluxDBClientError
import configparser
import os
import sys


prev_temp = 0
prev_fullenergy = 0
prev_curpower = 0

dir_path = os.path.dirname(os.path.realpath(__file__))

config = configparser.ConfigParser()
config.read(dir_path + "/" + 'boiler_mq2db.conf', encoding="utf8")
sections = config.sections()

InfluxDBHost = config['settings']['InfluxDBHost']
InfluxDBPort = config['settings']['InfluxDBPort']
InfluxDBUser = config['settings']['InfluxDBUser']
InfluxDBPass = config['settings']['InfluxDBPass']
InfluxDBDatabase = config['settings']['InfluxDBDatabase']

MqttHost = config.get('settings', 'MqttHost')
MqttPort = config.getint('settings', 'MqttPort')
MqttUser = config.get('settings', 'MqttUser')
MqttPass = config.get('settings', 'MqttPass')




def write_data(json_body):
    try:
        client = InfluxDBClient(InfluxDBHost, InfluxDBPort, InfluxDBUser, InfluxDBPass, InfluxDBDatabase, timeout=10)
    except Exception as err:
        flash("Entry was not recorded. Influx connection error: %s" % str(err))
    if client:
        print("Write points: {0}".format(json_body))
        try:
            client.write_points(json_body)
        except Exception as err:
            print('InfluxDBClientError = ' + str(err))


############
def on_message(mqttc, userdata, message):
    global prev_temp, prev_fullenergy, prev_curpower
    print("message received " ,str(message.payload.decode("utf-8")))
    print("message topic=",message.topic)
    print("message qos=",message.qos)
    print("message retain flag=",message.retain)

    if message.topic == "boiler/temp":
       temp = float(message.payload.decode("utf-8"))

       if temp != prev_temp:
          print(temp)
          json_body = [
          {
            "measurement": "boiler",
            "tags": {
                "host": "home",
                "region": "ua"
            },
            "fields": {
                "temp": temp
                 }
            }
          ]
          write_data(json_body)


          print("get temp=" + str(message.payload.decode("utf-8")))
          prev_temp = temp

    if message.topic == "boiler/fullenergy":
       fullenergy = float(message.payload.decode("utf-8"))

       if fullenergy != prev_fullenergy:
          print(fullenergy)
          json_body = [
          {
            "measurement": "boiler",
            "tags": {
                "host": "home",
                "region": "ua"
            },
            "fields": {
                "fullenergy": fullenergy
                 }
            }
          ]
          write_data(json_body)


          print("get fullenergy=" + str(message.payload.decode("utf-8")))
          prev_fullenergy = fullenergy

    if message.topic == "boiler/curpower":
       curpower = float(message.payload.decode("utf-8"))

       if curpower != prev_curpower:
          print(curpower)
          json_body = [
          {
            "measurement": "boiler",
            "tags": {
                "host": "home",
                "region": "ua"
            },
            "fields": {
                "curpower": curpower
                 }
            }
          ]
          write_data(json_body)


          print("get curpower=" + str(message.payload.decode("utf-8")))
          prev_curpower = curpower



def on_connect(mqttc, userdata, flags, rc):
    print("Connection returned result: "+str(rc))
    mqttc.subscribe("boiler/temp")

def on_subscribe(mqttc, obj, mid, granted_qos):
    print("Subscribed: " + str(mid) + " " + str(granted_qos))


def main():
    print("creating new instance")

    mqttc = mqtt.Client()
    mqttc.on_connect = on_connect
    mqttc.on_message = on_message
    mqttc.on_subscribe = on_subscribe

    #client = mqtt.Client("local") #create new instance
    #client.on_message=on_message #attach function to callback
    #print("connecting to broker")

    mqttc.username_pw_set(MqttUser, MqttPass)
    mqttc.connect(MqttHost, MqttPort, 60)
    mqttc.subscribe("boiler/temp")
    mqttc.subscribe("boiler/fullenergy")
    mqttc.subscribe("boiler/curpower")

    #time.sleep(5)

    mqttc.loop_forever()

    #time.sleep(60) # wait
    #mqttc.loop_stop() #stop the loop

if __name__ == "__main__":
    # execute only if run as a script
    main()

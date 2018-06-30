import paho.mqtt.client as mqtt #import the client1
import time
from influxdb import InfluxDBClient
from influxdb.client import InfluxDBClientError
import configparser
import os
import sys
from apscheduler.schedulers.background import BackgroundScheduler
import re
import requests
import json
import datetime


prev_temp = 0
prev_fullenergy = 0
prev_curpower = 0
power_range = range(0, 2000)
temp_range = range(0, 84)

dir_path = os.path.dirname(os.path.realpath(__file__))

config = configparser.ConfigParser()
config.read(dir_path + "/" + 'boiler_mq2db.conf', encoding="utf8")
sections = config.sections()

InfluxDBHost     = config['settings']['InfluxDBHost']
InfluxDBPort     = config['settings']['InfluxDBPort']
InfluxDBUser     = config['settings']['InfluxDBUser']
InfluxDBPass     = config['settings']['InfluxDBPass']
InfluxDBDatabase = config['settings']['InfluxDBDatabase']

MqttHost = config.get('settings', 'MqttHost')
MqttPort = config.getint('settings', 'MqttPort')
MqttUser = config.get('settings', 'MqttUser')
MqttPass = config.get('settings', 'MqttPass')

BlynkUrl    = config.get('settings', 'BlynkUrl')
BlynkPort   = config.get('settings', 'BlynkPort')
BlynkToken  = config.get('settings', 'BlynkToken')

def load_dirty_json(dirty_json):
    regex_replace = [(r"([ \{,:\[])(u)?'([^']+)'", r'\1"\3"'), (r" False([, \}\]])", r' false\1'), (r" True([, \}\]])", r' true\1')]
    for r, s in regex_replace:
        dirty_json = re.sub(r, s, dirty_json)
        clean_json = json.loads(dirty_json)
    return clean_json


def post_2blynk(pin, pin_value):
    #print("Check response...")
    try:
        response = requests.get(BlynkUrl + ':' + BlynkPort + '/' + BlynkToken + '/update/' + pin + '?value='+ pin_value, timeout=(15, 15))
        response.raise_for_status()

    except requests.exceptions.ReadTimeout:
        print('Oops. Read timeout occured')

    except requests.exceptions.ConnectTimeout:
        print('Oops. Connection timeout occured!')

    except requests.exceptions.ConnectionError:
        print('Seems like dns lookup failed..')

    except requests.exceptions.HTTPError as err:
        print('Oops. HTTP Error occured')
        print('Response is: {content}'.format(content=err.response.content))

    #print("Response status code: " + str(response.status_code))
    text = response.text


def every_minute():
    #print("Every minute")
    d = datetime.date.today()
    month = '{:02d}'.format(d.month)
    day = '{:02d}'.format(d.day)
    year = d.year

    #check energy today month
    std = readInflux("SELECT (fullenergy) FROM boiler WHERE time >= \'" + str(year) + "-" + month + "-" + day +"T00:00:00Z\'" + " limit 1 tz('Europe/Kiev')")
    stm = readInflux("SELECT (fullenergy) FROM boiler WHERE time >= \'" + str(year) + "-" + month + "-01T00:00:00Z\'" + " limit 1 tz('Europe/Kiev')")
    end = readInflux("SELECT (fullenergy) FROM boiler WHERE time <= now() ORDER BY time DESC limit 1 tz('Europe/Kiev')")

    #print(std)
    stdata = load_dirty_json(str(std.raw))
    stmdata = load_dirty_json(str(stm.raw))
    endata = load_dirty_json(str(end.raw))

    value_d = int(endata['series'][0]['values'][0][1]) - int(stdata['series'][0]['values'][0][1])
    value_m = int(endata['series'][0]['values'][0][1]) - int(stmdata['series'][0]['values'][0][1])

    post_2blynk('V9', str(value_d/1000))
    post_2blynk('V10', str(value_m/1000))

def readInflux(query):
    #print(query)
    result = ''
    try:
        client = InfluxDBClient(InfluxDBHost, InfluxDBPort, InfluxDBUser, InfluxDBPass, InfluxDBDatabase, timeout=10)
    except Exception as err:
        flash("Influx connection error: %s" % str(err))
    if client:
        #print("Read points:")
        try:
            result = client.query(query)
        except Exception as err:
            print('InfluxDBClientError = ' + str(err))
    return result



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
          if temp in temp_range:
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
          if curpower in power_range:
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

    scheduler = BackgroundScheduler()
    scheduler.add_job(every_minute, 'interval', seconds=60)
    scheduler.start()

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

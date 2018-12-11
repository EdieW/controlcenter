#!/usr/bin/env python3
# Copyright 2017 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Run a recognizer using the Google Assistant Library.

The Google Assistant Library has direct access to the audio API, so this Python
code doesn't need to record audio. Hot word detection "OK, Google" is supported.

It is available for Raspberry Pi 2/3 only; Pi Zero is not supported.
"""

import logging
import platform
import subprocess
import sys
import socket
import threading
import time
import datetime

import RPi.GPIO as GPIO

import aiy.assistant.auth_helpers
from aiy.assistant.library import Assistant
import aiy.audio
import aiy.voicehat
from google.assistant.library.event import EventType
from google.cloud import translate

_relay = 4
_translate_on = 0
_tlang = 'es'
_udp_poll = 0.5
_t = 0.0
_h = 0
_sprinkler_on = 0
_cnt = 0
_sock = 0
_peeraddress = None 

logging.basicConfig(
    level=logging.INFO,
    format="[%(asctime)s] %(levelname)s:%(name)s:%(message)s"
)

def sprinkler(t,h,son):
  #aiy.audio.say("temperature {} humidity {}".format(t, h))
  if (_sprinkler_on != son):
    light(_relay,son)
    if (son == 1):
       aiy.audio.say("Turn on sprinkler")
    else:
       aiy.audio.say("Turn off sprinkler")

def udpinit():
    # Create a UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setblocking(0)
    # Bind the socket to the port
    server_address = ('0.0.0.0', 17002)
    print('starting up on {} port {}'.format(*server_address))
    sock.bind(server_address)

    # open a file
    text_file = open("Output.txt", "a")
    # udp polling timer
    udppoll(sock, text_file)
    return sock

def udpsend(type, val):
    str = type + val
    _sock.sendto(str.encode(), _peeraddress)

def udppoll(sock, text_file):
  global _t
  global _h
  global _cnt

  threading.Timer(_udp_poll, udppoll,[sock, text_file]).start()
  try:
      global _peeraddress
      data, _peeraddress = sock.recvfrom(1024)
      print('received {} bytes from {}'.format(
        len(data), _peeraddress))
      print(data)

      if data:
        str = str(datetime.now()) + " " + data.decode()
        text_file.write(str + "\n")
        temperature,humidity,son = str.split(',')
        _t = float(temperature)
        _h = int(humidity)
        sonval = int(son) 
        sent = sock.sendto("OK".encode(), _peeraddress)
        print('sent {} bytes back to {}'.format(
            sent, _peeraddress))
        sprinkler(_t, _h, sonval)
  except socket.error:
      if _cnt % 10 == 0:
         print("no data")
      _cnt = _cnt + 1

def power_off_pi():
    aiy.audio.say('Good bye!')
    subprocess.call('sudo shutdown now', shell=True)


def reboot_pi():
    aiy.audio.say('See you in a bit!')
    subprocess.call('sudo reboot', shell=True)

def light(pin, on):
    if on == 1:
        GPIO.output(pin,GPIO.HIGH)
        aiy.audio.say('Turn on the light')
    else:
        GPIO.output(pin, GPIO.LOW)
        aiy.audio.say('Turn off the light')

def say_ip():
    ip_address = subprocess.check_output("hostname -I | cut -d' ' -f1", shell=True)
    aiy.audio.say('My IP address is %s' % ip_address.decode('utf-8'))

def findwords(text, word1, word2):
    w1 = text.find(word1)
    if w1 != -1:
       w1 = text.find(word2, w1)
    return w1

def blink_pi(tm):
    aiy.audio.say('Blinking')
    for x in range(0, 5):
        GPIO.output(_relay,GPIO.HIGH)
        time.sleep(tm)
        GPIO.output(_relay, GPIO.LOW)
        time.sleep(tm)

def translate1(translate_client, text, target):
    # The target language
    print("Translating to ", target, "....")

    translation = translate_client.translate(
        text,
        target_language=target)

    # print(u'Text: {}'.format(text))
    print(u'Translation: {}'.format(translation['translatedText']))
    # en-US en-GB de-DE es-ES fr-FR it-IT
    if target != "zh":
        target = target+"-"+target.upper()
        aiy.audio.say(translation['translatedText'], lang=target)


def process_event(assistant, event, translate_client):
    status_ui = aiy.voicehat.get_status_ui()
    if event.type == EventType.ON_START_FINISHED:
        status_ui.status('ready')

        #start udp listener
        global _sock
        _sock = udpinit()

        # use USB speaker instead of headphone jack
        # aiy.audio.set_audio_device("sysdefault:CARD=2")
        aiy.audio.say("I am ready")
        #aiy.audio.say("Este es un mejor amigo", lang='es-ES')
        if sys.stdout.isatty():
            print('Say "OK, Google" then speak, or "Goodbye" to quit...')

    elif event.type == EventType.ON_CONVERSATION_TURN_STARTED:
        status_ui.status('listening')

    elif event.type == EventType.ON_RECOGNIZING_SPEECH_FINISHED and event.args:
        print('You said:', event.args['text'])
        text = event.args['text'].lower()
        global _translate_on
        global _tlang
        if _translate_on == 1:
            assistant.stop_conversation()
            if findwords(text, "turn off", "translat") != -1:
                _translate_on = 0
                aiy.audio.say("Translation off", lang="en-US")
            else:
                translate1(translate_client, text, _tlang)
        elif text == 'power off':
            assistant.stop_conversation()
            power_off_pi()
        elif text == 'reboot':
            assistant.stop_conversation()
            reboot_pi()
        elif text == 'ip address':
            assistant.stop_conversation()
            say_ip()
        elif text.find('link') != -1:
            tm = 1
            if text.find('half') != -1:
                tm = 0.5
            elif text.find('quarter') != -1:
                tm = 0.25
            elif text.find('two') != -1:
                tm = 2
            elif text.find('three') != -1:
                tm = 3
            elif text.find('four') != -1:
                tm = 4
            elif text.find('five') != -1:
                tm = 5
            assistant.stop_conversation()
            blink_pi(tm)
        elif text == 'goodbye':
            sys.exit(0)
        elif findwords(text, 'turn on', 'light') != -1:
            assistant.stop_conversation()
            light(_relay,1)
        elif findwords(text, 'turn off', 'light') != -1:
            assistant.stop_conversation()
            light(_relay,0)
        elif findwords(text, 'sprinkler', 'state') != -1:
            assistant.stop_conversation()
            aiy.audio.say("temperature {} humidity {}".format(_t, _h))
        elif findwords(text, 'turn on', 'translat') != -1:
            assistant.stop_conversation()
            _translate_on=1
            if text.find('chinese') != -1:
               _tlang = 'zh'
               aiy.audio.say("Translator is in Chinese")
            elif text.find('italian') != -1:
               _tlang = 'it'
               aiy.audio.say("Translator is in Italian")
            elif text.find('french') != -1:
               _tlang = 'fr'
               aiy.audio.say("Translator is in French")
            elif text.find('german') != -1:
               _tlang = 'de'
               aiy.audio.say("Translator is in German")
            else:
               _tlang = 'es'
               aiy.audio.say("Translator is in Spanish")

    elif event.type == EventType.ON_END_OF_UTTERANCE:
        status_ui.status('thinking')

    elif (event.type == EventType.ON_CONVERSATION_TURN_FINISHED
          or event.type == EventType.ON_CONVERSATION_TURN_TIMEOUT
          or event.type == EventType.ON_NO_RESPONSE):
        status_ui.status('ready')

    elif event.type == EventType.ON_ASSISTANT_ERROR and event.args and event.args['is_fatal']:
        sys.exit(1)


def main():
    if platform.machine() == 'armv6l':
        print('Cannot run hotword demo on Pi Zero!')
        exit(-1)

    GPIO.setmode(GPIO.BCM)

    GPIO.setup(_relay, GPIO.OUT)
    GPIO.output(_relay, GPIO.LOW)

    #blink_pi(1)

    # Instantiates a client
    translate_client = translate.Client()
    #translate1(translate_client, "Hello World", "zh-CN")

    credentials = aiy.assistant.auth_helpers.get_assistant_credentials()
    with Assistant(credentials) as assistant:
        for event in assistant.start():
            process_event(assistant, event, translate_client)

    GPIO.cleanup()


if __name__ == '__main__':
    main()

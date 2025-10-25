#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys, os, time, json
import numpy as np
from scipy.integrate import solve_ivp
from dotenv import load_dotenv
from datetime import datetime
from copy import deepcopy
import traceback
load_dotenv()

from meals_model import Meals
from insulin_model import Insulin
from bergman_model import Bergman
from mqtt import MQTT


class VP_MQTT(MQTT):
    def __init__(self, host, port, username, password, topics, profile, no_sync=False):
        super().__init__(host, port, username, password)
        self.time_step = 0
        self.topics = topics

        self.disp_interval = 1  # display interval in seconds
        self.patient_profile = profile
        self._parse_profile()

        self.no_sync = no_sync
        self.sync_profile = no_sync

        self.client.message_callback_add(f'{topics["VP_ATTRIBUTE_TOPIC"]}/#', self.on_message_profile)
        self.client.message_callback_add(topics["INSULIN_TOPIC"], self.on_message_insulin)
        self.connect()

    def on_connect(self, client, userdata, flags, reason_code, properties):
        print(f">>> Connected with result code {reason_code}!")

        if not self.no_sync:
            # Subscribe to the topics
            client.subscribe(f'{self.topics["VP_ATTRIBUTE_TOPIC"]}/request/+', qos=1)
            client.subscribe(self.topics["INSULIN_TOPIC"], qos=1)

            print("Waiting for synchronizing Patient Profile with OpenAPS...")

    def _parse_profile(self):
        # Update patient type
        diabetic = self.patient_profile['diabetic']
        self.patient_type = 'diabetic' if diabetic else 'normal'

        # Update the meal schedule
        meals = self.patient_profile['meals']
        self.meals = []
        for meal in meals:
            self.meals.append((float(meal['time']), float(meal['carbs']), float(meal['duration'])))
        self.meal_function = Meals(self.meals)

        # Update the bolus insulins
        bolus_insulins = self.patient_profile['bolus_insulins']
        self.bolus_insulins = []
        for insulin in bolus_insulins:
            self.bolus_insulins.append((float(insulin['time']), float(insulin['dose']), float(insulin['duration'])))
        self.insulin_function = Insulin(bolus_insulin=self.bolus_insulins)

        print(f">>> Virtual Patient Profile: \n{self.patient_type=}, \n{self.meals=}, \n{self.bolus_insulins=}")

        # Initialize the Bergman model
        self.bergman = Bergman(type=self.patient_type, meals=self.meal_function, insulin=self.insulin_function)

        # Update bergman parameters
        if 'bergman_param' in self.patient_profile:
            params = self.patient_profile['bergman_param']
            self.bergman.update_params(params)

        print(f">>> Customized Bergman Parameters: \n{params=}")
        
        # Simulation settings, initialize once, do not update
        if 'sim_settings' in self.patient_profile:
            sim_settings = self.patient_profile['sim_settings']

            self.disp_interval = float(sim_settings['disp_interval'])
            self.simu_interval = int(sim_settings['simu_interval'])
            self.simu_length = int(sim_settings['simu_length'])
            init_state = sim_settings['init_state']
            self.init_state = [float(init_state['G0']), float(init_state['X0']), float(init_state['I0'])]
            
            # Simulation storage
            self.solution = np.zeros((self.simu_length, 3))

            # Initial conditions
            self.solution[0, :] = self.init_state

            print(f">>> Simulation Settings: \n{self.disp_interval=}, \n{self.simu_interval=}, \n{self.simu_length=}, \n{self.init_state=}")

    def on_message_profile(self, client, userdata, message):
        client.publish(message.topic.replace('/request/', '/response/'), json.dumps({'PatientProfile': self.patient_profile}), qos=1)
        print("\nPatient Profile was synchronized to OpenAPS.")
        self.sync_profile = True

    def on_message_insulin(self, client, userdata, message):
        # print(f"Received insulin message: {message.payload.decode()}")
        # the basal insulin message handler 
        self.insulin_rate = float(json.loads(message.payload.decode('utf-8'))['insulin_rate'])
        if hasattr(self, 'insulin_function'): 
            self.insulin_function.update_basal_rate(self.insulin_rate)
            print("+", end='', flush=True)
        
    def loop_forever(self):
        try:
            print("Press CTRL+C to exit the simulation loop...")
            self.loop_start()

            while True:
                time.sleep(self.disp_interval)

                print('.', end='', flush=True)
                if not self.sync_profile:
                    continue
                
                # simulate one step here
                t = self.time_step * self.simu_interval
                t_next = t + self.simu_interval
                self.time_step += 1
                res = solve_ivp(self.bergman.ode, (t, t_next), self.solution[self.time_step-1, :], args=())
                self.solution[self.time_step, :] = deepcopy(res.y[:, -1])
                
                # virtual CGM sensor, send to openAPS
                data = {
                    'Glucose': self.solution[self.time_step][0],
                    'time': self.time_step * self.simu_interval,
                }
                self.client.publish(self.topics['CGM_TOPIC'], json.dumps(data), qos=1)
                
                # Dashboard for visualization
                ts = self.time_step * self.simu_interval
                data = {
                    'timestamp': ts,
                    'Insulin': self.solution[self.time_step][2],
                    'Glucose': self.solution[self.time_step][0],
                    'Basal': self.insulin_rate * 1000 if hasattr(self, 'insulin_rate') else 0,  # metric convert: U/L -> mU/L
                }
                self.client.publish(self.topics['VP_TELEMETRY_TOPIC'], json.dumps(data), qos=1)

                if self.time_step >= self.simu_length - 1:
                    print("\n>>> Simulation completed.")
                    break
                
        except Exception as e:
            print(f"{repr(e)}")
            traceback.print_exc()
        finally:
            print(">>> Disconnecting from the MQTT broker")
            self.loop_stop()
            self.disconnect()
    
    
def main(no_sync=False):        
    MQTT_HOST = os.getenv('MQTT_HOST')
    MQTT_PORT = int(os.getenv('MQTT_PORT'))
    USERNAME = os.getenv('USERNAME')
    PASSWORD = os.getenv('PASSWORD')

    team_name = os.getenv('TEAM_NAME')
    if team_name is None or team_name == '':
        print('Error: TEAM_NAME is not set in the environment variables.')
        sys.exit(1)

    topic_prefix = f'cis441-541/{team_name}'

    topics = {
        'VP_ATTRIBUTE_TOPIC': f'{topic_prefix}/vp-attributes',
        'VP_TELEMETRY_TOPIC': f'{topic_prefix}/vp-telemetry',
        'INSULIN_TOPIC': os.getenv('INSULIN_TOPIC', f'{topic_prefix}/insulin-pump'),
        'CGM_TOPIC': os.getenv('CGM_TOPIC', f'{topic_prefix}/cgm'),
    }
    print(f'>>> Topic settings from environment: \n{topics=}')

    profile = {}
    try:
        with open('patient_profile.json', 'r') as infile:
            profile = json.load(infile)
    except Exception as e:
        print(f"Warning: Could not load patient_profile.json: {e}")
        sys.exit(1)

    print(f'>>> Patient Profile: \n{profile=}')

    vp_mqtt = VP_MQTT(MQTT_HOST, MQTT_PORT, USERNAME, PASSWORD, topics, profile, no_sync)
    vp_mqtt.loop_forever()
    

if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] not in ["0", "1"]:
        print("Usage: python main.py <0|1>\n\t0: run without OpenAPS\n\t1: run with OpenAPS")
        sys.exit(0)

    no_sync = sys.argv[1] == "0"

    main(no_sync)
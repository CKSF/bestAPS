import paho.mqtt.client as mqtt
import sys

class MQTT:
    def __init__(self, host, port, username, password):
        self.host = host
        self.port = port
        self.username = username
        self.password = password
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        self.client.username_pw_set(self.username, self.password)
            
    def connect(self):
        if self.client.connect(self.host, self.port, 60) != 0:
            print("Couldn't connect to the mqtt broker")
            sys.exit(1)
        else:
            print("Connected to the MQTT broker")
    
    def on_connect(self, client, userdata, flags, reason_code, properties):
        print(f"Connected with result code {reason_code}")

    def on_message(self, client, userdata, message):
        print(f"Received unsuportted message: {message.payload.decode()}")
        # Implement the message handler here

    def subscribe(self, topic, qos):
        self.client.subscribe(topic, qos=qos)

    def publish(self, topic, payload, qos):
        self.client.publish(topic, payload, qos=qos)

    def loop_start(self):
        self.client.loop_start()

    def loop_stop(self):
        self.client.loop_stop()

    def disconnect(self):
        self.client.disconnect()
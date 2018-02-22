import octoprint.plugin

class OxtionPlugin(octoprint.plugin.StartupPlugin,octoprint.plugin.EventHandlerPlugin):
	_repeat_timer = None

	def __init__(self):
		self.mqtt_publish = lambda *args, **kwargs: None
		self.mqtt_subscribe = lambda *args, **kwargs: None
		self.mqtt_unsubscribe = lambda *args, **kwargs: None

	def on_after_startup(self):
		helpers = self._plugin_manager.get_helpers("mqtt", "mqtt_publish", "mqtt_subscribe", "mqtt_unsubscribe")
		if helpers:
			if "mqtt_publish" in helpers:
				self.mqtt_publish = helpers["mqtt_publish"]
			if "mqtt_subscribe" in helpers:
				self.mqtt_subscribe = helpers["mqtt_subscribe"]
			if "mqtt_unsubscribe" in helpers:
				self.mqtt_unsubscribe = helpers["mqtt_unsubscribe"]

		self.mqtt_publish("oxtion/misc", "Oxtion plugin startup")
		self._logger.info("Oxtion Plugin started.")
		self.mqtt_publish("oxtion/led/mode", "0") # Mode: Startup
		self.mqtt_publish("octoled/mode", "0") # Mode: Startup

	def on_event(self, event, payload):
		self.mqtt_publish("oxtion/misc", "event: " + event)
		if event in ["Connected", "PrintDone"] :
			self.mqtt_publish("oxtion/led/mode", "1"); # Mode: Standby
			self.mqtt_publish("octoled/mode", "1"); # Mode: Standby 
		if event == "Disconnected":
			self.mqtt_publish("oxtion/led/mode", "4"); # Mode: Disconnected
			self.mqtt_publish("octoled/mode", "4"); # Mode: Disconnected
		if event == "PrintStarted":
			self.mqtt_publish("oxtion/led/mode", "2"); # Mode: Printing
			self.mqtt_publish("octoled/mode", "2"); # Mode: Printing
			self._repeat_timer = octoprint.util.RepeatedTimer(15, self.send_progress)
			self._repeat_timer.start()
			self._logger.info("Oxtion Plugin progress reporting started.")  
		if event in ["PrintFailed", "Error"] :
			self.mqtt_publish("oxtion/led/mode", "3"); # Mode: Error
			self.mqtt_publish("octoled/mode", "3"); # Mode: Error 
		if event in ["PrintFailed", "PrintDone"] :
			if self._repeat_timer != None:
				self._repeat_timer.cancel()
				self._repeat_timer = None

	def handle_Z150(self, comm_instance, phase, cmd, cmd_type, gcode, *args, **kwargs):
		if cmd.startswith("Z150 "):
			self._logger.info("Z150 Detected: " + cmd)
			self.mqtt_publish("oxtion/led/rgb", cmd[5:])
			self.mqtt_publish("octoled/rgb", cmd[5:])
			return None,
		if cmd.startswith("Z151 "):
			self._logger.info("Z151 Detected: " + cmd)
			self.mqtt_publish("oxtion/led/mode", cmd[5:])
			self.mqtt_publish("octoled/mode", cmd[5:])
			return None,

	def send_progress(self):
		self._logger.info("Oxtion Plugin progress reporting triggered.");
		if not self._printer.is_printing():
			return
		currentData = self._printer.get_current_data()
		if (currentData["progress"]["printTimeLeft"] == None):
			currentData["progress"]["printTimeLeft"] = currentData["job"]["estimatedPrintTime"]
		if (currentData["progress"]["printTime"] == None):
			currentData["progress"]["printTime"] = 0
		self.mqtt_publish("oxtion/misc", "estimate");
###		self.mqtt_publish("oxtion/estimate", "{ \"progress\": {1}, \"printtime\": {2}, \"timeleft\": {3} }".format(currentData["progress"]["completion"], currentData["progress"]["printTime"], currentData["progress"]["printTimeLeft"]));
		self.mqtt_publish("oxtion/estimate", "{ \"progress\": "+str(currentData["progress"]["completion"])+", \"printtime\": "+str(currentData["progress"]["printTime"])+", \"printtimeleft\": "+str(currentData["progress"]["printTimeLeft"])+" }");


##__plugin_implementations__ = [OxtionPlugin()]
##__plugin_hooks__ = { "octoprint.comm.protocol.gcode.queuing": __plugin_implementation__.HandleZ150 }

__plugin_name__ = "Oxtion"

def __plugin_load__():
	global __plugin_implementation__
	__plugin_implementation__ = OxtionPlugin()

	global __plugin_hooks__
	__plugin_hooks__ = {
		"octoprint.comm.protocol.gcode.queuing": __plugin_implementation__.handle_Z150
	}


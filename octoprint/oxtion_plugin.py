import octoprint.plugin

class OxtionPlugin(octoprint.plugin.StartupPlugin,octoprint.plugin.EventHandlerPlugin,octoprint.plugin.SimpleApiPlugin):
	_repeat_timer = None

	def __init__(self):
		self.mqtt_publish = lambda *args, **kwargs: None
		self.mqtt_subscribe = lambda *args, **kwargs: None
		self.mqtt_unsubscribe = lambda *args, **kwargs: None

	def get_api_commands(self):
		return dict(dummy=[])

	def on_api_get(self, request):
		from flask import jsonify
		from octoprint.server import fileManager
		import octoprint.filemanager
		import octoprint.filemanager.util
		import octoprint.filemanager.storage
		from octoprint.filemanager import FileDestinations

		req_path = "/"
		if request.args.get('path'):
			req_path = request.args.get('path')

		if fileManager.folder_exists(FileDestinations.LOCAL, req_path):
			files=fileManager.list_files(path=req_path, filter=None, recursive=False)

			tmp_f = []
			tmp_d = []
			if req_path != "/":
				tmp_d = [".."]
			for f in files["local"]:
				if files["local"][f]["type"] == "folder":
					tmp_d.append(files["local"][f]["name"])
				if files["local"][f]["type"] == "machinecode":
					n = files["local"][f]["name"];
					succ = "n/a";
					if "history" in files["local"][f]:
						history = files["local"][f]["history"]
						last = None
						for entry in history:
							if not last or ("timestamp" in entry and "timestamp" in last and entry["timestamp"] > last["timestamp"]):
								last = entry
						if last:
							if last["success"]:
								succ = "succ";
							else:
								succ = "err";

					tmp_f.append([n,succ])

			result = dict()
			result["path"] = req_path
			result["files"] = sorted(tmp_f, key=lambda s: s[0].lower())
			result["directories"] = sorted(tmp_d, key=lambda s: s.lower())
			
			return jsonify(result)
		return None

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
		self.mqtt_publish("oxtion/startup", "startup")

	def on_event(self, event, payload):
		if event == "PrintStarted":
			self._repeat_timer = octoprint.util.RepeatedTimer(60, self.send_progress)
			self._repeat_timer.start()
			self._logger.info("Oxtion Plugin progress reporting started.")  
		if event in ["PrintFailed", "PrintDone"] :
			if self._repeat_timer != None:
				self._repeat_timer.cancel()
				self._repeat_timer = None

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


__plugin_name__ = "Oxtion"

def __plugin_load__():
	global __plugin_implementation__
	__plugin_implementation__ = OxtionPlugin()


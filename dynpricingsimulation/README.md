# EPEX Spot Market Price 
--------------------------------
This plugin provides the current EPEX Spot Market Price. The data is fetched from the Consolinno Flexa API.

## Configuration
The plugin requires the following configuration in the file `/etc/nymea/flexa.conf`:
```
serverURL=<FLEXA_API_URL>
token=<TOKEN>
```
## Request behavior
The plugin request the price once on instantiation.
At a random time between 13:00 (CET) and 13:30 (CET) it starts to request Day-Ahead data each 5 minutes until the data is available.
There will be no request until the next day.

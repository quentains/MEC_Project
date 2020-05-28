from scipy import stats
import socket
import time

SLOPE_THRESHOLD = 0
VALVE_OPENING_TIME = 600
VALUE_LEN = 30
SOCKET_PORT = 5678

def get_slope(values):
    # Wait 30 data messages before computing the slope
    if len(values) >= VALUE_LEN:
        air = [x[0] for x in values]
        time = [x[1] for x in values]
        return stats.linregress(air, time)[0] # Thanks scipy
    else:
        return None

def rotate_value(values, new_value, receive_time):
    values.append((new_value, receive_time))
    if len(values) <= VALUE_LEN:
        return values
    else:
        return values[1:]

def parse_message(message):
    # Message of the form "SRV[air_quality][node_id]"
    if message[:3] == "SRV" :
        return int(message[3:5]), message[5:]
    else:
        return None, None

def send_order(target_socket, target_id, order):
    message = "COM{}{}".format(1 if order == "open" else 0, target_id)
    target_socket.send(message.encode('utf-8'))

if __name__ == '__main__':
    sensor_data = {}
    sensor_timer = {}
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(('', SOCKET_PORT))
    s.listen()
    while True:
        c, addr = s.accept()
        while True:
            message = c.recv(8)
            if not message: #Connection closed
                break
            message = message.decode('utf-8')
            now = time.time()
            new_value, target_id = parse_message(message)
            if target_id is None or new_value is None: #Bad messages should be logged
                continue
            if target_id not in sensor_data: #New node
                sensor_data[target_id] = []
                sensor_timer[target_id] = -1
            sensor_data[target_id] = rotate_value(sensor_data[target_id], new_value, now) #Update data

            slope = get_slope(sensor_data[target_id])
            print(slope)
            if sensor_timer[target_id] != -1 and now - sensor_timer[target_id] >= VALVE_OPENING_TIME: #Need to re-evaluate the valve
                if slope is not None and slope < SLOPE_THRESHOLD: #We can close the valve
                    send_order(c, target_id, "close")
                    sensor_timer[target_id] = -1
                else: #we keep it open for another 10 mins
                    sensor_timer[target_id] = now
            elif sensor_timer[target_id] == -1 and slope is not None and slope > SLOPE_THRESHOLD: #Need to open the valve
                send_order(c, target_id, "open")
                sensor_timer[target_id] = now

from scipy import stats

SLOPE_THRESHOLD = 5
VALVE_OPENING_TIME = 10
VALUE_LEN = 30

def get_slope(values):
    if len(values) > 1:
        air = [x[0] for x in values]
        time = [x[1] for x in values]
        return stats.linregress(air, time)[0]
    else: #TODO
        return -9999

def rotate_value(values, new_value):
    print(len(values))
    values.append(new_value)
    if len(values) <= VALUE_LEN:
        return values
    else:
        return values[1:]

def parse_message(message):
    #TODO
    return message[0], message[1]

def send_order(target_id, order): #TODO
    pass

if __name__ == '__main__':
    sensor_data = {}
    sensor_timer = {}
    # Open communication
    while True:
        # Wait for message
        # Example : message = [1, (i,i)]
        target_id, new_value = parse_message(message)
        if target_id not in sensor_data: #New node
            sensor_data[target_id] = []
            sensor_timer[target_id] = -1
        sensor_data[target_id] = rotate_value(sensor_data[target_id], new_value) #Update data
        if sensor_timer[target_id] != -1: #If we are waiting to close the valve, we decrease the count.
            sensor_timer[target_id] -= 1

        if sensor_timer[target_id] == 0: #Need to re-evaluate the valve
            if get_slope(sensor_data[target_id]) < SLOPE_THRESHOLD: #We can close the valve
                send_order(target_id, "close")
                sensor_timer[target_id] = -1
            else: #we keep it open for another 10 mins
                sensor_timer[target_id] = VALVE_OPENING_TIME
        elif sensor_timer[target_id] == -1 and get_slope(sensor_data[target_id]) >= SLOPE_THRESHOLD: #Need to open the valve
            send_order(target_id, "open")
            sensor_timer[target_id] = VALVE_OPENING_TIME

/* Game Controller */
#include <mbed.h>
#include <EthernetInterface.h>
#include <rtos.h>
#include <mbed_events.h>
#include <FXOS8700Q.h>
#include <C12832.h>

/* display */
C12832 lcd(D11, D13, D12, D7, D10);

/* event queue and thread support */
Thread dispatch;
EventQueue periodic;

/* Accelerometer */
I2C i2c(PTE25, PTE24);xVelocity
FXOS8700QAccelerometer acc(i2c, FXOS8700CQ_SLAVE_ADDR1);

/* Input from Potentiometers */
AnalogIn  left(A0); // For variable throttle control

/* Digital Joystick input */
DigitalIn joyUp(A2); // Digital throttle
DigitalIn joyLeft(A4); // Digital roll
DigitalIn joyRight(A5); // Digital roll

/* User input states */
float throttle = 0;
float roll = 0;

/* Function to determine if digital button is pressed */
bool isPressed(DigitalIn button) {
  if (!button.read()) {
    return false;
  }
  else {
    return true;
  }
}

/* Task for polling sensors */
void user_input(void) {

  // Get accelerometer information
  motion_data_units_t a;
  acc.getAxis(a);

  // Joystick throttle
  if(isPressed(joyUp)) {
    throttle = 100;
  }
  else {
    // Change in throttle - left Potentiometer
    throttle = left.read() * 100;

    // Allows throttle to reach 100
    if (throttle >= 99.5) {
      throttle = 100;
    }
  }

  // Digital roll control from joystick
  if(isPressed(joyLeft)) {
    roll =- 1;
  }
  else if (isPressed(joyRight)) {
    roll =+ 1;
  }
  else {
    // Angle for accelerometer roll control
    float magnitude = sqrt((a.x*a.x) + (a.y*a.y) + (a.z*a.z));
    a.x = a.x/magnitude;
    float angle = asin(a.x);

    // Set angle deadband
    if (angle <= 0.1 && angle >= -0.1) {
    angle = 0;
    }

    roll = -(angle); // Orientation
  }
}

/* Lander */
float altitude = 0;
float fuel = 100;
bool isFlying = 0;
bool crashed = 0;
int orientation = 0;
int xVelocity = 0;
int yVelocity = 0;

/* IP address */
SocketAddress lander("192.168.80.6",65200);
SocketAddress dash("192.168.80.6",65250);

EthernetInterface eth;
UDPSocket udp;

/* Task for synchronous UDP communications with lander */
void communications(void){
  SocketAddress source;

  /* Format the message to send to the Lander */
  char buffer[512];
  sprintf(buffer, "command:!\nthrottle:%d\nroll:%1.3f",int(throttle),roll); // No Spaces

  /* Send and recieve messages */
  udp.sendto( lander, buffer, strlen(buffer));

  nsapi_size_or_error_t  n = udp.recvfrom(&source, buffer, sizeof(buffer));
  buffer[n] = '\0';

  /* Unpack incomming message */
  char *nextline, *line;

  for(
    line = strtok_r(buffer, "\r\n", &nextline);
    line != NULL;
    line = strtok_r(NULL, "\r\n", &nextline)
  ) {

    /* Split into key value pairs */
    char *key, *value;
    key = strtok(line, ":");
    value = strtok(NULL, ":");

    /* Convert value strings into state variables */  // PUT LCD STUFF IN WHILE INSTEAD?
    if(strcmp(key,"altitude")==0) {
      altitude = atof(value);
    }
    else if(strcmp(key, "fuel")==0) {
      fuel = atof(value);
    }
    else if(strcmp(key, "flying")==0) {
      isFlying = atoi(value);
    }
    else if(strcmp(key, "crashed")==0) {
      crashed = atoi(value);
    }
    else if(strcmp(key, "orientation")==0) {
      orientation = atoi(value);
    }
    else if(strcmp(key, "Xy")==0) {
      xVelocity = atoi(value);
    }
    else if(strcmp(key, "vY")==0) {
      yVelocity = atoi(value);
    }
  }
}

/* Task for asynchronous UDP communications with dashboard */
void dashboard(void){

  /* Create and format a message to the Dashboard */
  SocketAddress source;
  char buffer[512];
  sprintf(buffer, "command:=\naltitude:%1.2f\nfuel:%1.2f\nflying:%d\ncrashed:%d\norientation:%d\nVx:%d\nVy:%d",
  altitude,fuel,isFlying,crashed,orientation,xVelocity, yVelocity); // No Spaces

  /* Send the message to the dashboard */
  udp.sendto( dash, buffer, strlen(buffer));

}

int main() {

  // Enable accelerometer
  acc.enable();

  /* ethernet connection : usually takes a few seconds */
  printf("Connecting \n");
  eth.connect();
  /* write obtained IP address to serial monitor */
  const char *ip = eth.get_ip_address();
  printf("IP address is: %s\n", ip ? ip : "No IP");

  /* udp communications ethernet */
  udp.open( &eth);

  printf("lander is on %s/%d\n",lander.get_ip_address(),lander.get_port() );
  printf("dash   is on %s/%d\n",dash.get_ip_address(),dash.get_port() );

  periodic.call_every(50, communications);
  periodic.call_every(50, dashboard);
  periodic.call_every(50, user_input);

  /* start event dispatching thread */
  dispatch.start( callback(&periodic, &EventQueue::dispatch_forever) );


}

/* Telefonzentrale für 4 analoge Apparate
  Simon Widmer, 2021

Additional Interrupts:
D8    PCINT0  (PCMSK0 / PCIF0 / PCIE0)
D9    PCINT1  (PCMSK0 / PCIF0 / PCIE0)
D10   PCINT2  (PCMSK0 / PCIF0 / PCIE0)
D11   PCINT3  (PCMSK0 / PCIF0 / PCIE0)
D12   PCINT4  (PCMSK0 / PCIF0 / PCIE0)
D13   PCINT5  (PCMSK0 / PCIF0 / PCIE0)
*/
#define I_DIAL A5
#define I_FORK1 5
#define I_FORK2 5
#define I_FORK3 5
#define I_FORK4 5
#define O_STATE A0
#define O_R1 7
#define O_R2 7
#define O_R3 7
#define O_R4 7
#define O_R5 3
#define O_R6 4
#define D_DELAY 10

#define ST_IDLE       0
#define ST_WAITINPUT  1
#define ST_WAITANSWER 2
#define ST_CONNECTED  3
#define ST_HANGUP     4
#define ST_CONFAILED  5

#define SIG_OFF       0
#define SIG_ON        1
#define SIG_BUSY      2
#define SIG_ERROR     3

float vers = 1.0;
//const byte NB_TEL = 4;

byte caller = 0;
byte called = 0;
byte state = ST_IDLE;
byte linesound = SIG_OFF;

byte last_input = 0;
bool dial_open = false; // Prüfen, ob eine Fertigstellung der Auswertung offen ist (Wählen / Gabel)
byte dial_counter = 0;
bool sig_state = HIGH;

// Timings
const int pulse = 60; //ms
const int pulse_min = pulse*0.9; //ms
const int pulse_max = pulse*1.1; //ms
const int pause = 40; //ms
const int pause_min = pause*0.9; //ms
const int pause_max = pause*1.1; //ms
const int period_max = 10* (pulse_max + pause_max);
const int call_max = 30*1000; // Dauer bis ein Anruf verworfen wird in ms

unsigned long ring_on = 0;
unsigned long ring_off = 0;

float dutycycle = 0.5;

unsigned long signal_beeper = 0; // Letzter Änderungszeitpunkt des Beeps
unsigned long timeout_fork = 0; // Letzter Änderungszeitpunkt der Gabel
unsigned long timeout_call = 0; // Dauer bis ein Anruf verworfen wird in ms


//----------------------------------------------------------------------


void handleSerial() {
 while (Serial.available() > 0) {
   char incomingCharacter = Serial.read();
   switch (incomingCharacter) {
     case 'a':
      digitalWrite(O_R1,HIGH);
      Serial.println("Set R1 High");
      break;
     case 'y':
      digitalWrite(O_R1,LOW);
      Serial.println("Set R1 Low");
      break;
     case 's':
      digitalWrite(O_R5,HIGH);
      Serial.println("Set R5 High");
      break;
     case 'x':
      digitalWrite(O_R5,LOW);
      Serial.println("Set R5 Low");
      break;
     case 'd':
      digitalWrite(O_R6,HIGH);
      Serial.println("Set R6 High");
      break;
     case 'c':
      digitalWrite(O_R6,LOW);
      Serial.println("Set R6 Low");
      break;
    }
 }
}


void print_variables(void){
  Serial.print("Sender: ");
  Serial.print(caller);
  Serial.print(" - Receiver: ");
  Serial.print(called);
  Serial.print(" Status: ");
  Serial.print(" Statusregister ");
  Serial.print(state);
  Serial.print(" Wählen: ");
  Serial.print(dial_open ? "1" : "0");
  Serial.print("(");
  Serial.print(dial_counter);
  Serial.println(")");
}


void reset_variables(void){
  caller = 0;
  called = 0;
  state = ST_IDLE;
  dial_open = false;
  dial_counter = 0;
  linesound = SIG_OFF;
  sig_state = HIGH;

  signal_beeper = 0;
  ring_on = 0;
  ring_off = 0;
  dutycycle = 0.5;
  
  digitalWrite(O_R1, LOW);
  digitalWrite(O_R2, LOW);
  digitalWrite(O_R3, LOW);
  digitalWrite(O_R4, LOW);
  digitalWrite(O_R5, HIGH);
  digitalWrite(O_R6, LOW);
}

void forkaction(byte mask, int call_id)
{
        // Hörer wurde abgehoben, Leitung frei, warte auf Nummerneingabe
      if (!mask && state == ST_IDLE){
        Serial.println("Hörer abgehoben");
        linesound = SIG_ON;
        state = ST_WAITINPUT;
        caller = call_id;
      }
      
      // Hörer wurde abgehoben, aber Leitung ist belegt
      else if (!mask && state != ST_IDLE && called != call_id && caller != call_id){
        Serial.println("Leitung belegt");
        linesound = SIG_BUSY;
      }
      
      // Hörer wird bei belegter Leitung wieder aufgelegt (kein Reset!!)
      else if (mask && state != ST_IDLE && called != call_id && caller != call_id){
        linesound = SIG_OFF;
      }
      
      // Hörer wurde vom Anrufer aufgelegt oder wählen im Gange
      else if (mask && state == ST_WAITINPUT && !dial_open && caller == call_id){
        linesound = SIG_OFF;
        timeout_fork = millis();
        dial_open = true;
        Serial.println("Aufgelegt oder Wählen");
      }
            
      // Impulse zählen auf fallender Flanke
      else if (!mask && state == ST_WAITINPUT && dial_open && caller == call_id){
        timeout_fork = millis();
        dial_counter += 1;
        //Serial.println("FALLING");
      }
      
      // ein neuer Impuls kommt rein, wählen im Gange
      else if (mask && state == ST_WAITINPUT && dial_open && caller == call_id){
        timeout_fork = millis();
        //Serial.println("RISING");
      }
            
      // Hörer wurde vom Anrufer aufgelegt oder wählen im Gange
      else if (mask && state == ST_WAITANSWER && caller == call_id){
        linesound = SIG_OFF;
        reset_variables();
        Serial.println("Anrufer hat Hörer aufgelegt (WAITANSWER), reset");
      }

      // Hörer wurde vom Anrufer während dem Klingln aufgelegt
      else if (mask && state == ST_WAITANSWER && caller == call_id){
        reset_variables();
        Serial.println("Anrufer hat Hörer aufgelegt (WAITANSWER), reset");
      }

//      // Telefonat für call_id, Hörer wurde abgehoben, Anruf hergestellt
//      else if (!mask && state == ST_WAITANSWER && called == call_id){
//        Serial.println("Angerufener hat abgehoben");
//        // Klingeln ausschalten bevor eigener Apparat auf Leitung geschaltet wird.
//        digitalWrite(O_R6, LOW);
//        setrelay(call_id, HIGH);
//        state = ST_CONNECTED;
//      }
      
      // Hörer wurde vom Angerufenen aufgelegt (bei hergestellter Verbindung)
      else if (mask && state == ST_CONNECTED && called == call_id){
        state = ST_HANGUP;
        called = 0;
        setrelay(call_id, LOW);
        linesound = SIG_ERROR;
        Serial.println("Angerufener hat Hörer aufgelegt");
      }

      // Hörer wird nach nicht erfolgreicher Verbindung aufgehängt
      else if (mask && state == ST_CONFAILED && caller == call_id){
        reset_variables();
        Serial.println("Anrufer hat Hörer aufgelegt (CONFAILED), reset");        
      }
}

void ringaction(byte mask)
{
        // Es war eine fallende Flanke
      if (!mask){
        if(ring_off == 0 || ring_on == 0){
          ring_off = millis();
        }
        // Kann erst Dutycycle berechnen wenn ring_off gefüttert war. Folglich muss auch mind. ring on erfasst sein.
        else{
          unsigned long now = millis();
          int period_net = now - ring_off;
          // Validiere Messung mittels Periodendauer Netzfrequenz
          if(period_net > 19 && period_net < 21)
          {
             int ontime = now-ring_on;
             if (ontime < 0) ontime = ontime *-1;
             //Serial.print("OFF: ti=");
             //Serial.print(ontime);
             //Serial.print(" T=");
             //Serial.println(period_net);
             dutycycle = (float)ontime/(float)period_net;
             //Serial.print(" G=");
             //Serial.println(dutycycle); 

          }
          ring_off = millis();
        }
      }
      // Es war eine steigende Flanke
      else{
        if(ring_off == 0 || ring_on == 0){
          ring_on = millis();
        }
        // Kann erst Dutycycle berechnen wenn ring_on gefüttert war. Folglich muss auch mind. ring off erfasst sein.
       else{
          unsigned long now = millis();
          int period_net = now - ring_on;
          // Validiere Messung mittels Periodendauer Netzfrequenz
          if(period_net > 19 && period_net < 21)
          {
             int ontime = ring_on-ring_off;
             if (ontime < 0) ontime = ontime *-1;
             //Serial.print("ON: ti=");
             //Serial.print(ontime);
             //Serial.print(" T=");
             //Serial.println(period_net);
             dutycycle = (float)ontime/(float)period_net;
             //Serial.print(" G=");
             //Serial.println(dutycycle); 
          }
          ring_on = millis();
        }
      }

      // Angerufener hat abgenommen, wenn dutycycle auf ca. 0.25 fällt
      if(dutycycle < 0.3){
        // Klingeln ausschalten bevor eigener Apparat auf Leitung geschaltet wird.
        digitalWrite(O_R6, LOW);
        setrelay(caller, HIGH);
        state = ST_CONNECTED;
        Serial.println("Verbindung hergestellt");
      }
}

bool check_inputs(void){
  byte current_input = get_inputs();
  if(current_input != last_input){
//    Serial.print("Counter: ");
//    Serial.println(dial_counter);
//    Serial.print (current_input, BIN);
//    Serial.print(" <- ");
//    Serial.println(last_input, BIN);
    //print_variables();
    byte mask_d = current_input & 0b00010000;
    byte mask_d_o = last_input & 0b00010000;
    byte mask_f1 = current_input &  0b00001000;
    byte mask_f1_o = last_input &  0b00001000;
    byte mask_f2 = current_input &  0b00000100;
    byte mask_f2_o = last_input &  0b00000100;
    byte mask_f3 = current_input &  0b00000010;
    byte mask_f3_o = last_input &  0b00000010;
    byte mask_f4 = current_input &  0b00000001;
    byte mask_f4_o = last_input &  0b00000001;
        
    // Gabelzustände---------------------------------------------------------------
    if ( mask_f1 != mask_f1_o) forkaction(mask_f1, 1);
    if ( mask_f2 != mask_f2_o) forkaction(mask_f2, 2);
    if ( mask_f3 != mask_f3_o) forkaction(mask_f3, 3);
    if ( mask_f4 != mask_f4_o) forkaction(mask_f4, 4);

    // Erkenne Klingeln und wann abgehoben wird.-------------------------------
    if ( mask_d != mask_d_o && state == ST_WAITANSWER) ringaction(mask_d);
  }
  last_input = current_input;
}

// Relais setzen fürs Klingeln
void setrelay(int nb, bool level){
    switch(nb){
      case 1:
        digitalWrite(O_R1, level);
        break;
      case 2:
        digitalWrite(O_R2, level);
        break;      
      case 3:
        digitalWrite(O_R3, level);
        break;      
      case 4:
        digitalWrite(O_R4, level);
        break;      
    }
}

byte get_inputs(void){
  byte input = 0;
  byte input2 = 0;
  // Poor Man's Debounce, overridden in state == ST_WAITANSWER
  if(state == ST_WAITANSWER){
    if (digitalRead(I_DIAL)) input += 0b10000;
  }
  else{
    do  {
      input = 0;
      if (digitalRead(I_DIAL)) input += 0b10000;
      if (digitalRead(I_FORK1)) input += 0b01000;
      if (digitalRead(I_FORK2)) input += 0b00100;
      if (digitalRead(I_FORK3)) input += 0b00010;
      if (digitalRead(I_FORK4)) input += 0b00001;
      delay(D_DELAY);
      input2 = 0;
      if (digitalRead(I_DIAL)) input2 += 0b10000;
      if (digitalRead(I_FORK1)) input2 += 0b01000;
      if (digitalRead(I_FORK2)) input2 += 0b00100;
      if (digitalRead(I_FORK3)) input2 += 0b00010;
      if (digitalRead(I_FORK4)) input2 += 0b00001;
    } while (input != input2);
  }
  return input;  
}

void check_timeout(void){
    // Auswertung der Gabel / Wählimpulse, egal von welcher Station
    if (state == ST_WAITINPUT && dial_open && millis() > (timeout_fork + pulse_max))
    {
      //Steht der Counter noch bei 0 so wurde die Gabel aufgehängt
      if (dial_counter == 0){
        reset_variables();
        Serial.println("Timeout: Hörer aufgelegt");
      }
      else{
        if(dial_counter==10){dial_counter=0;}
        if (check_number(dial_counter))
        {
          state = ST_WAITANSWER;
          called = dial_counter;
          setrelay(called, HIGH);
          digitalWrite(O_R6, HIGH);
          timeout_call = millis();
          dial_open = false;
          Serial.print("Rufe Station ");
          Serial.print(called);
          Serial.println(" an.");
        }
        else
        {
          Serial.println("Ungültige Nummer, reset dial Counter");
          tone(O_STATE,100);
          delay(100);
          linesound = SIG_OFF;
          dial_open = false;
          dial_counter = 0;
        }
      }
      timeout_fork = 0;
  }
  // Timeout wegen unbeantwortetem Anruf
  if (state == ST_WAITANSWER && millis() > (timeout_call + call_max))
  {
    state = ST_CONFAILED;
    // Klingeln ausschalten, Angerufenen zurückschalten
    digitalWrite(O_R6, LOW);
    setrelay(called, LOW);
    linesound = SIG_ERROR;
  }
}

bool check_number(int nb){
  if (nb>0 && nb < 5 && nb != caller){
    return true;
  }
  else {
    return false;
  }
}

void start_call(byte number){
  
}


void soundhandler(void){
  // Wählton und Besetztton, beide 425Hz. 
  //Wählton kontinuierlich, Besetztton 480ms ein/aus, Fehler 240ms ein/aus
  switch(linesound){
    case SIG_OFF:
      noTone(O_STATE);
      break;
    case SIG_ON:
      tone(O_STATE,425);
      break;
    case SIG_BUSY:
      beeper(480);
      break;
    case SIG_ERROR:
      beeper(240);
      break;
  }
}

void beeper(int duration)
{
  if (signal_beeper == 0)
  {
    tone(O_STATE,425);
    signal_beeper = millis();
  }
  else
  {
    if (millis() > (signal_beeper + duration))
    {
      if (sig_state)
      {
        noTone(O_STATE);
        signal_beeper = millis();
        sig_state = LOW;
      }
      else
      {
        tone(O_STATE,425);
        signal_beeper = millis();
        sig_state = HIGH;
      }
    }
    
  }
}

//-----------------------------------------------------------------------------------------

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.print("Telefonzentrale ");
  Serial.println (vers);
  Serial.print("_|‾|_ Pulsmin: ");
  Serial.print (pulse_min);
  Serial.print(" Pulsmax: ");
  Serial.print (pulse_max);
  Serial.print("       ‾|_|‾ Pausemin: ");
  Serial.print (pause_min);
  Serial.print(" Pausemax: ");
  Serial.print (pause_max);
  Serial.print(" Periodemax: ");
  Serial.println (period_max);

  pinMode(O_R1, OUTPUT);
  pinMode(O_R5, OUTPUT);
  pinMode(O_R6, OUTPUT);
  pinMode(O_STATE, OUTPUT);
  pinMode(I_FORK1, INPUT_PULLUP);
  pinMode(I_DIAL, INPUT_PULLUP);

  reset_variables();
  last_input = get_inputs();
}

void loop() {
   handleSerial();
   check_inputs();
   check_timeout();
   soundhandler();
}

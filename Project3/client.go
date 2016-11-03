package main

import (
	"bufio"
	"bytes"
	"fmt"
	"net"
	"os"
	//	"strings"
	"time"
)

var running bool // global variable if client is running

// read from connection and return true if ok
func Read(con net.Conn) string {
	buf := make([]byte, 2048)
	_, err := con.Read(buf)
	if err != nil {
		con.Close()
		running = false
		return "Error in reading!"
	}
	n := bytes.IndexByte(buf, 0)
	str := string(buf[:n])
	return string(str)
}

// clientsender(): read from stdin and send it via network
func clientsender(cn net.Conn, name []byte) {
	reader := bufio.NewReader(os.Stdin)
	for {
		//fmt.Print(name)
		input, _ := reader.ReadBytes('\n')
		if bytes.Equal(input, []byte("/quit\n")) {
			cn.Write([]byte("/quit"))
			running = false
			break
		}
		cn.Write([]byte(input[0 : len(input)-1]))
	}
}

// clientreceiver(): wait for input from network and print it out
func clientreceiver(cn net.Conn) {
	//	error_message := "You have been removed from the server.\n"
	for running {
		fmt.Println(Read(cn))
		//		if strings.Compare(fmt.Sprintln(Read(cn)), error_message) == 0 {
		//			fmt.Println(Read(cn))
		//			cn.Close()
		//		}
	}
}

func main() {
	running = true

	// connect
	destination := "127.0.0.1:9988"
	cn, _ := net.Dial("tcp", destination)
	defer cn.Close()

	// get the user name
	fmt.Print("Please give you name: ")
	reader := bufio.NewReader(os.Stdin)
	name, _ := reader.ReadBytes('\n')

	//cn.Write(strings.Bytes("User: "));
	cn.Write(name[0 : len(name)-1])

	// start receiver and sender
	go clientreceiver(cn)
	go clientsender(cn, name)

	// wait for quiting (/quit). run until running is true
	for running {
		time.Sleep(1 * 1e9)
	}
}

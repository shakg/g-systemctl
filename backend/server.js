var express = require('express')
let cors = require('cors')
let app = express()
let s = require("shelljs")

app.use(cors())

app.get('/', function (req, res) {
  const services = s.exec(`systemctl list-units -t service --full --all --plain --no-legend --no-pager --output=json`);
  return res.send(JSON.parse(services))
})

app.listen(3000, function () {
  console.log('CORS-enabled web server listening on port 3000')
})

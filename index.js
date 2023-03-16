let s = require("shelljs");

isActive = function(service_name){
    const is_active = s.exec(`systemctl is-active ${service_name}`);
    return is_active.stdout;
}

getAllServices = function(){
    const services = s.exec(`systemctl list-units -t service --full --all --plain --no-legend --no-pager --output=json-pretty`);
    return JSON.parse(services.stdout);
}

exports.isActive;
exports.getAllServices;
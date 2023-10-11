// Define a variable to store the fetched data
let serviceData = [];

// Function to render the data on the web page
function renderData(data) {
    const serviceList = document.getElementById("service-list");
    serviceList.innerHTML = "";

    data.forEach(service => {
        const _div = document.createElement("div");
        _div.textContent = `${service.unit} - ${service.sub}`;
        _div.style.margin = '2px';
        _div.style.display = 'flex';
        _div.style.gap = '5px';
        _div.style.flexDirection = 'row';
        _div.style.alignItems = 'center';
//        _div.style.justifyContent = 'center';

        const _button = document.createElement("button");
        _button.style.padding = "4px";
        _button.style.color = "black";
        _button.style.backgroundColor = "white";

        _div.appendChild(_button);
        
        if(service.sub === "running"){
            _div.style.backgroundColor = 'green';
            _button.innerText = "STOP";
            _button.onclick = async() => {
                await fetch('/service?open=false&service_name='+service.unit);
                fetchData();
            }
        }else{
            _button.innerText = "START";
            _button.onclick = async () => {
                await fetch('/service?open=true&service_name='+service.unit);
                fetchData();
            }
        }

        serviceList.appendChild(_div);
    });
}

// Fetch data from the server and render it initially
async function fetchData() {
    try {
        // Use the fetch API to make a GET request to your server endpoint
        const response = await fetch('/api'); // Replace '/api' with your actual server endpoint

        // Check if the response status is OK (HTTP 200)
        if (!response.ok) {
            throw new Error(`Failed to fetch data: ${response.status} ${response.statusText}`);
        }

        // Parse the response as JSON
        const data = await response.json();

        // Store the fetched data in the serviceData variable
        serviceData = data;

        // Render the fetched data
        renderData(serviceData);
    } catch (error) {
        console.error('Error fetching data:', error);
    }
}


// Filter data based on user input
function filterData() {
    const filterInput = document.getElementById("filter");
    const filterValue = filterInput.value.toLowerCase();

    const filteredData = serviceData.filter(service =>
        service.unit.toLowerCase().includes(filterValue) ||
        service.description.toLowerCase().includes(filterValue)
    );

    renderData(filteredData);
}

// Initial fetch and add event listener for filtering
document.addEventListener("DOMContentLoaded", () => {
    fetchData();

    const filterInput = document.getElementById("filter");
    filterInput.addEventListener("input", filterData);
});

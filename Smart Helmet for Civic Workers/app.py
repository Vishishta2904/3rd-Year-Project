from flask import Flask, request, jsonify, render_template, send_from_directory
from datetime import datetime
from flask_cors import CORS  # Import CORS for cross-origin requests

app = Flask(__name__)
CORS(app)  # Enable CORS for all routes

# Temporary storage (in-memory)
data_log = []

@app.route('/endpoint', methods=['POST', 'GET', 'OPTIONS'])
def receive_data():
    if request.method == 'OPTIONS':
        # Handle preflight request
        response = app.make_default_options_response()
        return response
        
    elif request.method == 'GET':
        return jsonify({"status": "success", "message": "Use POST to send data"}), 200
        
    elif request.method == 'POST':
        try:
            data = request.get_json()
            if not data:
                return jsonify({"status": "error", "message": "No data provided"}), 400
                
            # Add timestamp for tracking
            data['timestamp'] = datetime.now().isoformat()
            
            # Print received data for debugging
            print("Received data:", data)
            
            # Save to in-memory log
            data_log.append(data)
            
            # Respond with success
            return jsonify({"status": "success", "message": "Data received"}), 200
        except Exception as e:
            print("Error:", e)
            return jsonify({"status": "error", "message": str(e)}), 400
    
    # Handle any other methods
    return jsonify({"status": "error", "message": "Method not allowed"}), 405

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/logs', methods=['GET'])
def get_logs():
    return jsonify(data_log), 200

# Error handlers
@app.errorhandler(404)
def not_found(error):
    return jsonify({"status": "error", "message": "Resource not found"}), 404

@app.errorhandler(405)
def method_not_allowed(error):
    return jsonify({"status": "error", "message": "Method not allowed"}), 405

@app.errorhandler(500)
def server_error(error):
    return jsonify({"status": "error", "message": "Internal server error"}), 500

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)